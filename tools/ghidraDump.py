import os
import json

# Dump all functions in the current program to a JSON file
function_list = []
fs = list(currentProgram.getFunctionManager().getFunctions(True))
for f in fs:
    name = f.getName()
    address = f.getEntryPoint()
    calling_convention = f.getCallingConventionName()
    return_type = f.getReturnType().getName()
    param_types = [str(x.getDataType()) for x in f.getParameters()]
    prototype_string = f.getPrototypeString(True, True)
    function_dict = {
        "name": name,
        "address": "0x" + str(address),
        "calling_convention": calling_convention,
        "return_type": return_type,
        "param_types": param_types,
        "prototype_string": prototype_string
    }
    function_list.append(function_dict)

# Write function_list to a JSON file
# File is created in the directory that you launched Ghidra from (on macOS, this is the directory that you ran ./ghidraRun from)
home_directory = os.path.expanduser("~")
file_path = os.path.join(home_directory, 'functions.json')
with open(file_path, 'w') as json_file:
    json.dump(function_list, json_file)


