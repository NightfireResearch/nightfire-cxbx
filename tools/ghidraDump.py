# Dump all functions in the current program to a JSON file
function_list = []
fs = list(currentProgram.getFunctionManager().getFunctions(True))
for f in fs:
    name = f.getName()
    address = f.getEntryPoint()
    calling_convention = f.getCallingConventionName()
    return_type = f.getReturnType().getName()
    param_types = [str(x.getDataType()) for x in f.getParameters()]
    function_list.append((name, "0x"+str(address), calling_convention, return_type, param_types))

# Write function_list to a JSON file
# File is created in the directory that you launched Ghidra from (on macOS, this is the directory that you ran ./ghidraRun from)
with open('functions.json', 'w') as json_file:
    json.dump(function_list, json_file)



