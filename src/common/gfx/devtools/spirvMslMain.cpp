// spirvmsl: the backend's SPIR-V to MSL translation (shaderMsl.h) without the game or a Mac.
//
//   spirvmsl <folder>
//
// Every .spv in the folder (spirvdump's output, or a run's spirv/ from NIGHTFIRE_SPIRVDUMP=1) is translated as the SDL
// GPU backend translates it for SDL's Metal driver and written beside it as .metal. Each is then checked against its
// SPIR-V: the math_mode pragma first; the entry point SDL is given; [[buffer(n)]] for each uniform block and
// [[texture(n)]], [[sampler(n)]] for each sampler, n its SPIR-V binding, and nothing else bound; [[attribute(n)]]
// for each vertex input location; [[user(locnN)]] for each stage input and output; no float +, - or * left as an
// operator where the SPIR-V marks every one NoContraction, and a spvFAdd, spvFSub or spvFMul call per NoContraction add, subtract or multiply and an fma per Fma
// whose result is used. Across the folder, every pixel program's inputs must be outputs of every vertex program. One
// line per module that fails, then a summary.

#include "../shaderMsl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <filesystem>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace {

enum {
    OpEntryPoint = 15, OpExtInstImport = 11, OpExtInst = 12, OpVariable = 59, OpDecorate = 71,
    OpFAdd = 129, OpFSub = 131, OpFMul = 133, OpFDiv = 136,
};
enum { DecorationBuiltIn = 11, DecorationLocation = 30, DecorationBinding = 33, DecorationDescriptorSet = 34,
       DecorationNoContraction = 42 };
enum { StorageUniformConstant = 0, StorageInput = 1, StorageUniform = 2, StorageOutput = 3 };
enum { GlslFma = 50 };

struct Module {
    bool vertex = false;
    std::set<uint32_t> noContraction, builtIn;
    std::vector<uint32_t> location, binding, set;   // by id, ~0 when undecorated
    std::vector<uint8_t> storage;                   // by id, 0xFF when not a variable
    uint32_t glsl = 0;
    // Operations whose results reach an output (SPIRV-Cross writes nothing for a value nothing uses), and those
    // that do not
    int add = 0, sub = 0, mul = 0, div = 0, plain = 0, fma = 0, dead = 0;
};

const uint32_t kNone = ~0u;

// Instructions without side effects whose first two operands are a type and a result: what a value nothing reads
// can be dropped from.
bool Pure(uint32_t op) {
    return op == OpExtInst || op == 61 /* Load */ || op == 65 || op == 66 /* AccessChain */ ||
           (op >= 77 && op <= 82) /* vector and composite */ || (op >= 86 && op <= 92) /* sampling */ ||
           (op >= 109 && op <= 124) /* conversions */ || (op >= 126 && op <= 141) /* arithmetic */ ||
           op == 148 /* Dot */ || (op >= 164 && op <= 191) /* logic, Select, comparisons */;
}

bool Parse(const std::vector<uint32_t> &w, Module *m) {
    if (w.size() < 5 || w[0] != 0x07230203u)
        return false;
    uint32_t bound = w[3];
    m->location.assign(bound, kNone);
    m->binding.assign(bound, kNone);
    m->set.assign(bound, kNone);
    m->storage.assign(bound, 0xFF);
    std::vector<size_t> code;   // the functions' instructions
    for (size_t i = 5; i < w.size();) {
        uint32_t op = w[i] & 0xFFFF, count = w[i] >> 16;
        if (count == 0 || i + count > w.size())
            return false;
        const uint32_t *o = &w[i + 1];
        switch (op) {
            case OpEntryPoint: m->vertex = o[0] == 0; break;
            case OpExtInstImport: if (strcmp((const char *)&o[1], "GLSL.std.450") == 0) m->glsl = o[0]; break;
            case OpVariable: m->storage[o[1]] = (uint8_t)o[2]; break;
            case OpDecorate:
                if (o[1] == DecorationNoContraction) m->noContraction.insert(o[0]);
                else if (o[1] == DecorationBuiltIn) m->builtIn.insert(o[0]);
                else if (o[1] == DecorationLocation) m->location[o[0]] = o[2];
                else if (o[1] == DecorationBinding) m->binding[o[0]] = o[2];
                else if (o[1] == DecorationDescriptorSet) m->set[o[0]] = o[2];
                break;
        }
        if (op == 54 /* Function */ || !code.empty())
            code.push_back(i);
        i += count;
    }
    // Drop pure instructions whose results nothing reads, until none is left
    std::vector<bool> live(code.size(), true);
    for (bool dropped = true; dropped;) {
        dropped = false;
        std::vector<uint32_t> uses(bound, 0);
        for (size_t k = 0; k < code.size(); k++) {
            if (!live[k]) continue;
            size_t i = code[k];
            uint32_t op = w[i] & 0xFFFF, count = w[i] >> 16;
            // the operands that are ids: not an extended instruction's number, nor the literal indices after a
            // shuffle's vectors or an extract's or insert's composite
            uint32_t end = op == 79 || op == 82 ? 5 : op == 81 ? 4 : count;
            for (uint32_t j = Pure(op) ? 3 : 1; j < end && j < count; j++)
                if (!(op == OpExtInst && j == 4) && w[i + j] < bound) uses[w[i + j]]++;
        }
        for (size_t k = 0; k < code.size(); k++) {
            size_t i = code[k];
            if (live[k] && Pure(w[i] & 0xFFFF) && uses[w[i + 2]] == 0) { live[k] = false; dropped = true; }
        }
    }
    for (size_t k = 0; k < code.size(); k++) {
        const uint32_t *o = &w[code[k] + 1];
        uint32_t op = w[code[k]] & 0xFFFF;
        bool counted = op == OpFAdd || op == OpFSub || op == OpFMul || op == OpFDiv ||
                       (op == OpExtInst && o[2] == m->glsl && o[3] == GlslFma);
        if (!counted) continue;
        if (!live[k]) { m->dead++; continue; }
        if (op == OpExtInst) m->fma++;
        else if (op == OpFDiv) m->div++;
        else if (!m->noContraction.count(o[1])) m->plain++;
        else if (op == OpFAdd) m->add++;
        else if (op == OpFSub) m->sub++;
        else m->mul++;
    }
    return true;
}

int Count(const std::string &s, const std::string &what) {
    int n = 0;
    for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + 1)) n++;
    return n;
}

// The numbers n of every "[[<attribute>(n)]]" in the source.
std::set<uint32_t> Slots(const std::string &s, const char *attribute) {
    std::set<uint32_t> slots;
    std::string key = std::string("[[") + attribute + "(";
    for (size_t at = s.find(key); at != std::string::npos; at = s.find(key, at + 1))
        slots.insert((uint32_t)strtoul(s.c_str() + at + key.size(), NULL, 10));
    return slots;
}

std::string SetText(const std::set<uint32_t> &s) {
    std::string t;
    for (uint32_t v : s) t += (t.empty() ? "" : ",") + std::to_string(v);
    return "{" + t + "}";
}

std::vector<uint32_t> ReadWords(const std::string &path) {
    std::vector<uint32_t> words;
    FILE *f = fopen(path.c_str(), "rb");
    if (f == NULL) return words;
    fseek(f, 0, SEEK_END);
    long bytes = ftell(f);
    fseek(f, 0, SEEK_SET);
    words.resize((size_t)bytes / 4);
    if (fread(words.data(), 4, words.size(), f) != words.size()) words.clear();
    fclose(f);
    return words;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: spirvmsl <folder of .spv>\n");
        return 2;
    }
    std::string folder = argv[1];
    std::vector<std::string> names;
    std::error_code failure;
    for (const std::filesystem::directory_entry &e : std::filesystem::directory_iterator(folder, failure)) {
        if (e.path().extension() == ".spv") names.push_back(e.path().stem().string());
    }
    if (failure) {
        fprintf(stderr, "cannot open %s\n", folder.c_str());
        return 2;
    }
    std::sort(names.begin(), names.end());

    int modules = 0, failed = 0, checks = 0;
    int totalNc = 0, totalFma = 0, totalDiv = 0, totalPlain = 0, totalDead = 0;
    std::set<uint32_t> pixelInputs, vertexOutputsCommon;
    bool anyVertex = false;
    for (const std::string &name : names) {
        std::vector<uint32_t> words = ReadWords(folder + "/" + name + ".spv");
        Module m;
        std::vector<std::string> problems;
        if (!Parse(words, &m)) {
            printf("%s: not a SPIR-V module\n", name.c_str());
            failed++;
            continue;
        }
        modules++;
        std::string msl, entry, error;
        if (!ShaderMsl_Translate(words.data(), words.size(), &msl, &entry, &error)) {
            printf("%s: translation failed: %s\n", name.c_str(), error.c_str());
            failed++;
            continue;
        }
        FILE *out = fopen((folder + "/" + name + ".metal").c_str(), "wb");
        if (out != NULL) { fwrite(msl.data(), 1, msl.size(), out); fclose(out); }

        auto check = [&](bool ok, const std::string &what) { checks++; if (!ok) problems.push_back(what); };
        check(msl.compare(0, 33, "#pragma METAL fp math_mode(safe)\n") == 0, "no math_mode pragma first");
        std::string head = std::string(m.vertex ? "vertex " : "fragment ");
        size_t e = msl.find(" " + entry + "(");
        check(e != std::string::npos && msl.rfind(head, e) != std::string::npos &&
              msl.find('\n', msl.rfind(head, e)) > e, "entry point " + entry + " not a " + head + "function");

        // Resources: n = the SPIR-V binding, and nothing else bound
        std::set<uint32_t> buffers, textures, attributes, inputs, outputs;
        for (uint32_t id = 0; id < m.storage.size(); id++) {
            uint8_t sc = m.storage[id];
            if (sc == StorageUniform) {
                buffers.insert(m.binding[id]);
                check(m.set[id] == (m.vertex ? 1u : 3u), "uniform block in set " + std::to_string(m.set[id]));
            } else if (sc == StorageUniformConstant) {
                textures.insert(m.binding[id]);
                check(m.set[id] == (m.vertex ? 0u : 2u), "sampler in set " + std::to_string(m.set[id]));
            } else if ((sc == StorageInput || sc == StorageOutput) && !m.builtIn.count(id) && m.location[id] != kNone) {
                if (sc == StorageInput && m.vertex) attributes.insert(m.location[id]);
                else (sc == StorageInput ? inputs : outputs).insert(m.location[id]);
            }
        }
        check(Slots(msl, "buffer") == buffers, "buffers " + SetText(Slots(msl, "buffer")) + ", wanted " + SetText(buffers));
        check(Slots(msl, "texture") == textures, "textures " + SetText(Slots(msl, "texture")) + ", wanted " + SetText(textures));
        check(Slots(msl, "sampler") == textures, "samplers " + SetText(Slots(msl, "sampler")) + ", wanted " + SetText(textures));
        check(Slots(msl, "attribute") == attributes,
              "attributes " + SetText(Slots(msl, "attribute")) + ", wanted " + SetText(attributes));
        std::set<uint32_t> users;
        for (uint32_t l : m.vertex ? outputs : inputs) users.insert(l);
        std::set<uint32_t> user;
        for (size_t at = msl.find("[[user(locn"); at != std::string::npos; at = msl.find("[[user(locn", at + 1))
            user.insert((uint32_t)strtoul(msl.c_str() + at + 11, NULL, 10));
        check(user == users, "user locations " + SetText(user) + ", wanted " + SetText(users));
        if (m.vertex) {
            if (!anyVertex) vertexOutputsCommon = outputs;
            std::set<uint32_t> both;
            for (uint32_t l : vertexOutputsCommon) if (outputs.count(l)) both.insert(l);
            vertexOutputsCommon = both;
            anyVertex = true;
        } else {
            pixelInputs.insert(inputs.begin(), inputs.end());
        }

        // NoContraction and Fma, in the entry point (after the helpers): no float +, - or * left as an operator in a
        // module whose every add, subtract and multiply is NoContraction (the only + allowed is an integer constant
        // index's; FXAA's glslang module has plain ones), and exactly one helper call or fma for each operation whose
        // result is used. SPIRV-Cross also writes a temporary for an unused value that other unused code reads
        // twice: the calls in temporaries nothing reads are not counted.
        size_t body = msl.find("\n" + head);
        std::string code = body != std::string::npos ? msl.substr(body) : msl;
        std::string operators = std::regex_replace(code, std::regex("int\\(_[0-9]+\\) \\+ [0-9]+"), "");
        check(m.plain != 0 || Count(operators, " + ") + Count(operators, " - ") + Count(operators, " * ") == 0,
              "+, - or * as an operator");
        std::vector<std::string> statements, temporaries;
        for (size_t at = 0; at < code.size();) {
            size_t end = code.find('\n', at);
            if (end == std::string::npos) end = code.size();
            statements.push_back(code.substr(at, end - at));
            std::smatch name;
            temporaries.push_back(std::regex_search(statements.back(), name, std::regex("^\\s*\\w+ (_[0-9]+) = "))
                                  ? name[1].str() : std::string());
            at = end + 1;
        }
        std::vector<bool> unused(statements.size(), false);
        for (bool more = true; more;) {
            more = false;
            for (size_t s = 0; s < statements.size(); s++) {
                if (temporaries[s].empty() || unused[s]) continue;
                std::regex reference("\\b" + temporaries[s] + "\\b");
                bool read = false;
                for (size_t r = 0; r < statements.size() && !read; r++)
                    read = r != s && !unused[r] && std::regex_search(statements[r], reference);
                if (!read) { unused[s] = true; more = true; }
            }
        }
        const char *names[4] = { "spvFAdd(", "spvFSub(", "spvFMul(", "fma(" };
        int used[4] = { m.add, m.sub, m.mul, m.fma };
        for (int k = 0; k < 4; k++) {
            int calls = 0;
            for (size_t s = 0; s < statements.size(); s++)
                if (!unused[s]) calls += Count(statements[s], names[k]);
            check(calls == used[k], std::string(names[k]) + ") calls " + std::to_string(calls) + " for " +
                  std::to_string(used[k]) + " operations");
        }
        check(Count(msl, "T spvFMul(T l") == 0 || msl.find("return fma(l, r, -T(0));") != std::string::npos,
              "spvFMul adds +0");
        totalNc += m.add + m.sub + m.mul;
        totalFma += m.fma;
        totalDiv += m.div;
        totalPlain += m.plain;
        totalDead += m.dead;

        if (!problems.empty()) {
            failed++;
            printf("%s:", name.c_str());
            for (const std::string &p : problems) printf(" [%s]", p.c_str());
            printf("\n");
        }
    }
    std::set<uint32_t> unfed;
    for (uint32_t l : pixelInputs) if (anyVertex && !vertexOutputsCommon.count(l)) unfed.insert(l);
    checks++;
    if (!unfed.empty()) {
        failed++;
        printf("pixel inputs %s are not outputs of every vertex program\n", SetText(unfed).c_str());
    }
    printf("[spirvmsl] %d modules, %d checks, %d differ; %d NoContraction adds/subtracts/multiplies, %d Fma, %d divides, "
           "%d contractible operations (%d more unused); vertex outputs common to all %s, pixel inputs %s\n",
           modules, checks, failed, totalNc, totalFma, totalDiv, totalPlain, totalDead,
           SetText(vertexOutputsCommon).c_str(), SetText(pixelInputs).c_str());
    return failed == 0 ? 0 : 1;
}
