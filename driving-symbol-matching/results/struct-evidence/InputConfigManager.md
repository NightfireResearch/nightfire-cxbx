# InputConfigManager

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none

Xbox methods (11):
  0x4ff50 undefined ParseDefFileForFrontEnd(undefined4 param_1, undefined4 param_2)
  0x50080 undefined __thiscall Shutdown(InputConfigManager * this)
  0x50140 undefined SetInverted(undefined1 param_1)
  0x50160 undefined IsInverted(void)
  0x50170 undefined GetNumConfigs(undefined4 param_1)
  0x50180 undefined GetCurrentConfig(undefined4 param_1)
  0x501a0 undefined GetLocaleID(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x50270 undefined * __stdcall Get(void)
  0x502d0 undefined ParseMasterConfigFile(undefined4 param_1)
  0x504d0 void __thiscall InitAndPreload(InputConfigManager * this)
  0x50720 undefined SetConfig(undefined4 param_1, undefined4 param_2)

PS2 methods (13):
  0x1664c0 InputConfigManager::Get
  0x166508 InputConfigManager::InputConfigManager
  0x166548 InputConfigManager::ParseMasterConfigFile
  0x166788 InputConfigManager::ParseDefFileForFrontEnd
  0x1668a0 InputConfigManager::InitAndPreload
  0x166b88 InputConfigManager::Shutdown
  0x166cb0 InputConfigManager::SetConfig
  0x166d60 InputConfigManager::SetInverted
  0x166d78 InputConfigManager::IsInverted
  0x166d80 InputConfigManager::GetNumConfigs
  0x166d90 InputConfigManager::GetCurrentConfig
  0x166dc0 InputConfigManager::GetLocaleID
  0x166f28 InputConfigManager::SetupInputMappings

Sheet rows:
  InputConfigManager::Get(void)
  InputConfigManager::InputConfigManager(void)
  InputConfigManager::ParseMasterConfigFile(char *)
  InputConfigManager::ParseDefFileForFrontEnd(ConfigInfo *, char
  InputConfigManager::InitAndPreload(void)
  InputConfigManager::Shutdown(void)
  InputConfigManager::SetConfig(InputConfigManager::ConfigType, i
  InputConfigManager::SetInverted(bool)
  InputConfigManager::IsInverted(void)
  InputConfigManager::GetNumConfigs(InputConfigManager::ConfigTyp
  InputConfigManager::GetCurrentConfig(InputConfigManager::Config
  InputConfigManager::GetLocaleID(InputConfigManager::ConfigType,
  InputConfigManager::SetupInputMappings(char *)

Xbox methods treated as members (11 of 11; untyped ones count when ECX is read before it is written): Get, GetCurrentConfig, GetLocaleID, GetNumConfigs, InitAndPreload, IsInverted, ParseDefFileForFrontEnd, ParseMasterConfigFile, SetConfig, SetInverted, Shutdown

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x004  w[4] R/W -> IOModule::GetIOModule [2: GetCurrentConfig@50180, SetConfig@50720]
  +0x010  w[4] LEA/W addr-taken [3: InitAndPreload@504d0, ParseMasterConfigFile@502d0, Shutdown@50080]
  +0x020  w[1] R/W [2: IsInverted@50160, SetInverted@50140]
  +0x024  w[4] R -> StringToNumber::ConvertStringToNumber [1: ParseDefFileForFrontEnd@4ff50]
  +0x028  w[4] R/W [2: InitAndPreload@504d0, SetConfig@50720]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] W [3: InitAndPreload@1668a0, InputConfigManager@166508, Shutdown@166b88]
  +0x004  w[4] R/W [4: GetCurrentConfig@166d90, InitAndPreload@1668a0, InputConfigManager@166508, SetConfig@166cb0]
  +0x008  w[4] LEA/W addr-taken [2: InitAndPreload@1668a0, SetConfig@166cb0]
  +0x00c  w[4] W [1: InitAndPreload@1668a0]
  +0x010  w[4] LEA/W addr-taken [3: InitAndPreload@1668a0, ParseMasterConfigFile@166548, Shutdown@166b88]
  +0x014  w[4] W [1: ParseMasterConfigFile@166548]
  +0x018  w[4] LEA/W addr-taken [4: InitAndPreload@1668a0, InputConfigManager@166508, ParseMasterConfigFile@166548, Shutdown@166b88]
  +0x01c  w[4] W [1: ParseMasterConfigFile@166548]
  +0x020  w[4] R/W [3: InitAndPreload@1668a0, IsInverted@166d78, SetInverted@166d60]
  +0x024  w[4] R/W -> StringToNumber::~StringToNumber, strtok [3: InitAndPreload@1668a0, InputConfigManager@166508, ParseDefFileForFrontEnd@166788]
  +0x028  w[4] R/W -> UFileLoader::FileFree [3: InitAndPreload@1668a0, InputConfigManager@166508, Shutdown@166b88]
  +0x02c  w[4] R/W -> UFileLoader::FileFree [2: InputConfigManager@166508, Shutdown@166b88]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  SetInverted: W +0x20 w1
  IsInverted: R +0x20 w1
  GetCurrentConfig: R +0x4 w4
