#ifndef DRIVING_PLATFORM_METHODPOINTER_H_
#define DRIVING_PLATFORM_METHODPOINTER_H_

// A method called through a plain function pointer: a vtable slot read as data, or a method's address. On x86 such
// a pointer is __fastcall with the unused EDX as its second parameter, which lines up with __thiscall (`this` in
// ECX, the rest on the stack). x86-64 has one convention, `this` first and the arguments after it, so there is no
// EDX parameter:
//     bool (METHOD_CALL *processData)(Processor *processor, METHOD_EDX UGroup *group, UData *data);
//     vtable->processData(processor, METHOD_EDX_ARG group, data);
// A pointer whose method takes no arguments may keep its plain `int`: the extra one is ignored on x86-64.
#if defined(_M_IX86) || defined(__i386__)
#define METHOD_CALL __fastcall
#define METHOD_EDX int,
#define METHOD_EDX_ARG 0,
#else
#define METHOD_CALL
#define METHOD_EDX
#define METHOD_EDX_ARG
#endif

#endif // DRIVING_PLATFORM_METHODPOINTER_H_
