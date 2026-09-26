# Vtables: 843 Xbox (405 stored by code), 382 PS2 from the sheet (3 rejected)

Confidence: certain 299, unresolved 64, probable 16

| PS2 vtable | row | slots | confidence | best Xbox | score | agree/clash | shape | named by ctor | runner-up score |
|---|---|---|---|---|---|---|---|---|---|
| AimedShootingAnimationController (0x00363b08) | 10922 | 2 | unresolved | 0x001a3288 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| AimedAnimationController (0x00363b28) | 10923 | 2 | unresolved | 0x001a3288 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| BlendedAnimationController (0x00363b48) | 10924 | 2 | probable | 0x00189e14 | 1.0 | 1/0 | 0.0 |  | -3.0 |
| CrossFadeAnimationController (0x00363b68) | 10925 | 2 | probable | 0x00189e14 | 1.0 | 1/0 | 0.0 |  | -3.0 |
| StandardAnimationController (0x00363b88) | 10926 | 2 | certain | 0x00189e0c | 11.0 | 1/0 | 0.0 | yes | -3.0 |
| AnimationController (0x00363ba8) | 10927 | 2 | unresolved | 0x0018beb4 | 1.0 | 1/0 | 0.0 |  | 1.0 |
| HeightCalcEvent (0x00364c88) | 10934 | 1 | unresolved | 0x001b3d78 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| DropWeaponEvent (0x00364ca0) | 10935 | 1 | unresolved | 0x001b3d78 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| FireEvent (0x00364cb8) | 10936 | 1 | unresolved | 0x001b3d78 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| DefaultEventHandler (0x00364cd0) | 10937 | 1 | unresolved | 0x001b3d78 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| AICharacterEnemy (0x00367800) | 10952 | 36 | unresolved | 0x0018aa18 | 4.12 | 3/0 | 0.56 |  | 4.09 |
| AICharacterBond (0x00367e38) | 10955 | 33 | certain | 0x0018a688 | 17.94 | 9/1 | 0.97 | yes | -18.53 |
| AICharacterEnemyDriver (0x003681f0) | 10957 | 33 | certain | 0x0018a788 | 13.94 | 5/1 | 0.97 | yes | -14.13 |
| AICharacterEnemyGround (0x00368648) | 10959 | 36 | certain | 0x0018a840 | 17.95 | 6/0 | 0.97 | yes | -12.76 |
| AICharacterEnemySnow (0x00368950) | 10961 | 36 | certain | 0x0018a948 | 9.95 | 8/0 | 0.97 |  | -10.61 |
| AICharacterEnemySSnow (0x00368c18) | 10963 | 36 | certain | 0x0018aa18 | 6.93 | 5/0 | 0.97 |  | -1.5 |
| AICharacterEnemyWindow (0x00369200) | 10967 | 36 | certain | 0x0018ab68 | 19.95 | 8/0 | 0.98 | yes | -10.15 |
| AICharacterHands (0x003696e0) | 10969 | 36 | certain | 0x0018ad28 | 7.95 | 6/0 | 0.98 |  | -4.78 |
| AICharacterPassenger (0x00369a28) | 10971 | 33 | certain | 0x0018ae68 | 11.93 | 3/1 | 0.96 | yes | -8.11 |
| AICharacterPedestrian (0x0036a148) | 10973 | 34 | certain | 0x0018b470 | 22.98 | 14/1 | 0.99 | yes |  |
| AIGroundVehicle (0x0036b568) | 10981 | 5 | unresolved | 0x001a76fc | -5.28 | 0/2 | 0.36 |  | -6.79 |
| AIHelicopter (0x0036b888) | 10983 | 5 | certain | 0x0018b950 | 12.98 | 4/1 | 0.99 | yes | -14.0 |
| AIVehicle (0x0036bd78) | 10988 | 5 | certain | 0x0018bb10 | 16.0 | 4/0 | 1.0 | yes | 0.8 |
| DebugIndexer (0x0036cde8) | 10996 | 21 | unresolved | 0x0018bd00 | 1.84 | 0/0 | 0.92 |  | -0.11 |
| DTuningDBMgr (0x0036d8f8) | 11006 | 3 | unresolved | 0x0018bfb4 | 1.37 | 0/0 | 0.69 |  | 0.87 |
| EWakeupSmackable (0x00370210) | 11015 | 1 | certain | 0x0018c83c | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EUnloadWeapon (0x00370228) | 11016 | 1 | certain | 0x0018c838 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ETwoWheelsOn (0x00370240) | 11017 | 1 | certain | 0x0018c834 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ETwoWheelsOff (0x00370258) | 11018 | 1 | certain | 0x0018c830 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ETimerDrawOn (0x00370270) | 11019 | 1 | certain | 0x0018c82c | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ETimerDrawOff (0x00370288) | 11020 | 1 | certain | 0x0018c828 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ETargetBeaconOnOff (0x003702a0) | 11021 | 1 | certain | 0x0018c824 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESwitchStingerChannel (0x003702b8) | 11022 | 1 | certain | 0x0018c820 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESwitchEffectOn (0x003702d0) | 11023 | 1 | certain | 0x0018c81c | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESwitchEffectOff (0x003702e8) | 11024 | 1 | certain | 0x0018c818 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESuppressEffect (0x00370300) | 11025 | 1 | certain | 0x0018c814 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EStreamEvent (0x00370318) | 11026 | 1 | certain | 0x0018c810 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EStopTimer (0x00370330) | 11027 | 1 | certain | 0x0018c80c | 10.0 | 0/0 | 0.0 | yes | 0.0 |
| EStopTimedAction (0x00370348) | 11028 | 1 | certain | 0x0018c808 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EStopSoundPos (0x00370360) | 11029 | 1 | certain | 0x0018c804 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EStopEffect (0x00370378) | 11030 | 1 | certain | 0x0018c800 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EStartTimer (0x00370390) | 11031 | 1 | certain | 0x0018c7fc | 10.0 | 0/0 | 0.0 | yes | 0.0 |
| EStartTimedAction (0x003703a8) | 11032 | 1 | certain | 0x0018c7f8 | 10.0 | 0/0 | 0.0 | yes | 0.0 |
| EStartMission (0x003703c0) | 11033 | 1 | certain | 0x0018c7f4 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EStartEffect (0x003703d8) | 11034 | 1 | certain | 0x0018c7f0 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESpawnVehicle (0x003703f0) | 11035 | 1 | unresolved | 0x001b3d78 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| ESpawnTraffic (0x00370408) | 11036 | 1 | certain | 0x0018c7ec | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESpawnSmackable (0x00370420) | 11037 | 1 | probable | 0x0018c7e8 | 1.0 | 1/0 | 0.0 |  | -3.0 |
| ESpawnSimplePhysics (0x00370438) | 11038 | 1 | certain | 0x0018c7e4 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESpawnSentry (0x00370450) | 11039 | 1 | certain | 0x0018c7e0 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESpawnForceEffect (0x00370468) | 11040 | 1 | certain | 0x0018c7c8 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESpawnExplosionStatic (0x00370480) | 11041 | 1 | certain | 0x0018c7c4 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESpawnExplosion (0x00370498) | 11042 | 1 | certain | 0x0018c7c0 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESpawnDramaticSmackable (0x003704b0) | 11043 | 1 | certain | 0x0018c7bc | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESimFrameUpdate (0x003704c8) | 11044 | 1 | certain | 0x0018c7b8 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESimEndFrame (0x003704e0) | 11045 | 1 | certain | 0x0018c7b4 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EShowInstanceStatic (0x003704f8) | 11046 | 1 | certain | 0x0018c7b0 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EShowInstance (0x00370510) | 11047 | 1 | certain | 0x0018c7ac | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EShake (0x00370528) | 11048 | 1 | certain | 0x0018c7a8 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetWaypoint (0x00370540) | 11049 | 1 | certain | 0x0018c7a4 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetVisualDamage (0x00370558) | 11050 | 1 | certain | 0x0018c7a0 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetVideo (0x00370570) | 11051 | 1 | certain | 0x0018c754 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetTimer (0x00370588) | 11052 | 1 | certain | 0x0018c750 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetTerrain (0x003705a0) | 11053 | 1 | certain | 0x0018c74c | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetSubtitle (0x003705b8) | 11054 | 1 | certain | 0x0018c748 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetSubRollDirection (0x003705d0) | 11055 | 1 | certain | 0x0018c744 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetSpecialWeapon (0x003705e8) | 11056 | 1 | certain | 0x0018c740 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetSimRate (0x00370600) | 11057 | 1 | certain | 0x0018c73c | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetRenderVariation (0x00370618) | 11058 | 1 | certain | 0x0018c738 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetReflectiveObject (0x00370630) | 11059 | 1 | certain | 0x0018c734 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetRecordedGameTime (0x00370648) | 11060 | 1 | certain | 0x0018c730 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetRecSpeedOffset (0x00370660) | 11061 | 1 | certain | 0x0018c72c | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetObjective (0x00370678) | 11062 | 1 | certain | 0x0018c728 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetNextPath (0x00370690) | 11063 | 1 | certain | 0x0018c724 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetNextAISpline (0x003706a8) | 11064 | 1 | certain | 0x0018c720 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetMissionMessage (0x003706c0) | 11065 | 1 | certain | 0x0018c71c | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetMagicCounterThreshold (0x003706d8) | 11066 | 1 | certain | 0x0018c718 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetMagicCounter (0x003706f0) | 11067 | 1 | certain | 0x0018c714 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetJumpGravity (0x00370708) | 11068 | 1 | certain | 0x0018c3ac | 7.0 | 0/1 | 0.0 | yes | -3.0 |
| ESetFrameAnimationTime (0x00370720) | 11069 | 1 | certain | 0x0018c70c | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetCullDistanceFactor (0x00370738) | 11070 | 1 | certain | 0x0018c3ac | 7.0 | 0/1 | 0.0 | yes | -3.0 |
| ESetCreakLevel (0x00370750) | 11071 | 1 | certain | 0x0018c700 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetCollisionGeometry (0x00370768) | 11072 | 1 | certain | 0x0018c6fc | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetBitMagicCounter (0x00370780) | 11073 | 1 | certain | 0x0018c6f8 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetAvoidZone (0x00370798) | 11074 | 1 | certain | 0x0018c6f4 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetAutoDriveCamOnMovie (0x003707b0) | 11075 | 1 | certain | 0x0018c6f0 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetAutoDriveCamOn (0x003707c8) | 11076 | 1 | certain | 0x0018c6e8 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetAutoDriveCamOffMovie (0x003707e0) | 11077 | 1 | certain | 0x0018c6e4 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetAutoDriveCamOff (0x003707f8) | 11078 | 1 | certain | 0x0018c6e0 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetAudioMix (0x00370810) | 11079 | 1 | certain | 0x0018c6dc | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetAreaBrightness (0x00370828) | 11080 | 1 | certain | 0x0018c6d8 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESetAFXMode (0x00370840) | 11081 | 1 | certain | 0x0018c6d4 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESentryMuzzleFlash (0x00370858) | 11082 | 1 | certain | 0x0018c6d0 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EScheduleEvent (0x00370870) | 11083 | 1 | certain | 0x0018c6cc | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EScaleFog (0x00370888) | 11084 | 1 | certain | 0x0018c6c8 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EScaleFieldOfView (0x003708a0) | 11085 | 1 | certain | 0x0018c6c4 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ESapphirePole (0x003708b8) | 11086 | 1 | probable | 0x0018c6c0 | 1.0 | 1/0 | 0.0 |  | -3.0 |
| ERollSub (0x003708d0) | 11087 | 1 | certain | 0x0018c6bc | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ERevertAudioMix (0x003708e8) | 11088 | 1 | certain | 0x0018c6b8 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EResume (0x00370900) | 11089 | 1 | certain | 0x0018c6b4 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EResetPlayerCarPos (0x00370918) | 11090 | 1 | certain | 0x0018c6ac | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EResetPlayerCar (0x00370930) | 11091 | 1 | certain | 0x0018c6a8 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EResetAudioMix (0x00370948) | 11092 | 1 | certain | 0x0018c6a4 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ERenderFrame (0x00370960) | 11093 | 1 | certain | 0x0018c6a0 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EReleaseStream (0x00370978) | 11094 | 1 | certain | 0x0018c69c | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ERandomExplosion (0x00370990) | 11095 | 1 | certain | 0x0018c698 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EProfileMissionSection (0x003709a8) | 11096 | 1 | certain | 0x0018c694 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPowerUpTopSpeed (0x003709c0) | 11097 | 1 | unresolved | 0x001b3d78 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| EPowerUpTimer (0x003709d8) | 11098 | 1 | certain | 0x0018c68c | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPowerUpShield (0x003709f0) | 11099 | 1 | certain | 0x0018c678 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPowerUpMultiDamage (0x00370a08) | 11100 | 1 | certain | 0x0018c674 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPowerUpInvulnerable (0x00370a20) | 11101 | 1 | unresolved | 0x001b3d78 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| EPowerUpHealth (0x00370a38) | 11102 | 1 | certain | 0x0018c66c | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPowerUpBraking (0x00370a50) | 11103 | 1 | unresolved | 0x001b3d78 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| EPowerUpAmmo (0x00370a68) | 11104 | 1 | certain | 0x0018c654 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPowerUp (0x00370a80) | 11105 | 1 | certain | 0x0018c644 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPlayerWin (0x00370a98) | 11106 | 1 | certain | 0x0018c640 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPlayerSetImmunity (0x00370ab0) | 11107 | 1 | certain | 0x0018c63c | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ERestart (0x00370ac8) | 11108 | 1 | certain | 0x0018c6b0 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPlayerLose (0x00370ae0) | 11109 | 1 | certain | 0x0018c6b0 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPlaySystemAnim (0x00370af8) | 11110 | 1 | certain | 0x0018c634 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPlaySoundPos (0x00370b10) | 11111 | 1 | certain | 0x0018c630 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPlaySound (0x00370b28) | 11112 | 1 | certain | 0x0018c620 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPlayPOVDeath (0x00370b40) | 11113 | 1 | certain | 0x0018c61c | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPlayGiottoEffect (0x00370b58) | 11114 | 1 | certain | 0x0018c618 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPlayEffect (0x00370b70) | 11115 | 1 | certain | 0x0018c614 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPlayCarCameraAnim (0x00370b88) | 11116 | 1 | certain | 0x0018c610 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPlayCameraSpline (0x00370ba0) | 11117 | 1 | certain | 0x0018c60c | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPlayCameraAnim (0x00370bb8) | 11118 | 1 | certain | 0x0018c608 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPlayActorEffect (0x00370bd0) | 11119 | 1 | certain | 0x0018c604 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPause (0x00370be8) | 11120 | 1 | certain | 0x0018c600 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPathSetTimeStatic (0x00370c00) | 11121 | 1 | certain | 0x0018c5fc | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPathSetTimeDynamic (0x00370c18) | 11122 | 1 | certain | 0x0018c5f8 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPathSetThrottleStatic (0x00370c30) | 11123 | 1 | certain | 0x0018c5f4 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPathSetThrottleDynamic (0x00370c48) | 11124 | 1 | certain | 0x0018c5f0 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPathSetRunningStatic (0x00370c60) | 11125 | 1 | certain | 0x0018c5ec | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPathSetRunningDynamic (0x00370c78) | 11126 | 1 | certain | 0x0018c5e8 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPathSetDesiredThrottleStatic (0x00370c90) | 11127 | 1 | certain | 0x0018c5e4 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPathSetDesiredThrottleDynamic (0x00370ca8) | 11128 | 1 | certain | 0x0018c5e0 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPathSetAccelerationStatic (0x00370cc0) | 11129 | 1 | certain | 0x0018c5dc | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPathSetAccelerationDynamic (0x00370cd8) | 11130 | 1 | certain | 0x0018c5d8 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPathSetAccelDelayStatic (0x00370cf0) | 11131 | 1 | certain | 0x0018c5d4 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPathSetAccelDelayDynamic (0x00370d08) | 11132 | 1 | certain | 0x0018c5d0 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPathSFXUpdate (0x00370d20) | 11133 | 1 | certain | 0x0018c5cc | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPathSFX (0x00370d38) | 11134 | 1 | certain | 0x0018c5b8 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPathMultiplyThrottleStatic (0x00370d50) | 11135 | 1 | certain | 0x0018c5b4 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPathMultiplyThrottleDynamic (0x00370d68) | 11136 | 1 | certain | 0x0018c5b0 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPathChangeThrottleStatic (0x00370d80) | 11137 | 1 | certain | 0x0018c5ac | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPathChangeThrottleDynamic (0x00370d98) | 11138 | 1 | certain | 0x0018c5a8 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EPassObjective (0x00370db0) | 11139 | 1 | unresolved | 0x001b3d78 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| EPassMissionObjective (0x00370dc8) | 11140 | 1 | unresolved | 0x001b3d78 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| EObjectiveSetIncomplete (0x00370de0) | 11141 | 1 | certain | 0x0018c59c | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EObjectiveSetCurrent (0x00370df8) | 11142 | 1 | certain | 0x0018c598 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EObjectivePass (0x00370e10) | 11143 | 1 | certain | 0x0018c594 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EObjectiveInsert (0x00370e28) | 11144 | 1 | certain | 0x0018c590 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EObjectiveFail (0x00370e40) | 11145 | 1 | certain | 0x0018c58c | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EObjectiveDisplayText (0x00370e58) | 11146 | 1 | certain | 0x0018c588 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EObjectiveAdd (0x00370e70) | 11147 | 1 | certain | 0x0018c584 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ENuclearBlast (0x00370e88) | 11148 | 1 | certain | 0x0018c580 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EMuzzleFlash (0x00370ea0) | 11149 | 1 | certain | 0x0018c57c | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EMaybeActivateAIElement (0x00370eb8) | 11150 | 1 | certain | 0x0018c578 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ELetterBoxOn (0x00370ed0) | 11151 | 1 | certain | 0x0018c574 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ELetterBoxOff (0x00370ee8) | 11152 | 1 | certain | 0x0018c570 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ELavaHaze (0x00370f00) | 11153 | 1 | certain | 0x0018c56c | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EKillSentry (0x00370f18) | 11154 | 1 | certain | 0x0018c568 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EKillObject (0x00370f30) | 11155 | 1 | certain | 0x0018c564 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EKillAIElement (0x00370f48) | 11156 | 1 | certain | 0x0018c560 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EKickObject (0x00370f60) | 11157 | 1 | certain | 0x0018c55c | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EKickAIElement (0x00370f78) | 11158 | 1 | certain | 0x0018c554 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EInsertObjective (0x00370f90) | 11159 | 1 | probable | 0x0018c550 | 1.0 | 1/0 | 0.0 |  | -3.0 |
| EInfraRedOn (0x00370fa8) | 11160 | 1 | certain | 0x0018c54c | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EInfraRedOff (0x00370fc0) | 11161 | 1 | certain | 0x0018c548 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EInflictDamage (0x00370fd8) | 11162 | 1 | certain | 0x0018c544 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EIncMagicCounter (0x00370ff0) | 11163 | 1 | certain | 0x0018c540 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EHitWindow (0x00371008) | 11164 | 1 | certain | 0x0018c53c | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EHideInstanceStatic (0x00371020) | 11165 | 1 | certain | 0x0018c538 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EHideInstance (0x00371038) | 11166 | 1 | certain | 0x0018c534 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EGadgetOn (0x00371050) | 11167 | 1 | certain | 0x0018c530 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EGadgetOff (0x00371068) | 11168 | 1 | certain | 0x0018c52c | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EForceNearPedRespawn (0x00371080) | 11169 | 1 | certain | 0x0018c528 | 11.0 | 1/0 | 0.0 | yes | 1.0 |
| EForceCarStop (0x00371098) | 11170 | 1 | certain | 0x0018c524 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EFireWeapon (0x003710b0) | 11171 | 1 | certain | 0x0018c520 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EFireTriggerSpeedCondition (0x003710c8) | 11172 | 1 | certain | 0x0018c510 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EFireRandomTrigger (0x003710e0) | 11173 | 1 | certain | 0x0018c50c | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EFireEventList (0x003710f8) | 11174 | 1 | certain | 0x0018c508 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EFailMissionObjective (0x00371110) | 11175 | 1 | unresolved | 0x001b3d78 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| EFESimFrameUpdate (0x00371128) | 11176 | 1 | certain | 0x0018c500 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EExit (0x00371140) | 11177 | 1 | certain | 0x0018c4fc | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EEndMission (0x00371158) | 11178 | 1 | certain | 0x0018c4f0 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EEndControlToSpline (0x00371170) | 11179 | 1 | certain | 0x0018c4ec | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EEndCarStop (0x00371188) | 11180 | 1 | certain | 0x0018c4e8 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EEndCameraAnim (0x003711a0) | 11181 | 1 | certain | 0x0018c4e4 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EEnableTrigger (0x003711b8) | 11182 | 1 | certain | 0x0018c4e0 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EEnablePowerUp (0x003711d0) | 11183 | 1 | certain | 0x0018c4dc | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EDropMine (0x003711e8) | 11184 | 1 | certain | 0x0018c4d8 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EDisplayText (0x00371200) | 11185 | 1 | certain | 0x0018c4d4 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EDisplayObjectiveText (0x00371218) | 11186 | 1 | probable | 0x0018c4d0 | 1.0 | 1/0 | 0.0 |  | -3.0 |
| EDisablePowerUp (0x00371248) | 11188 | 1 | certain | 0x0018c4c8 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EDeactivateAIElement (0x00371260) | 11189 | 1 | certain | 0x0018c4c4 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EDamagePlayer (0x00371278) | 11190 | 1 | certain | 0x0018c4c0 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ECustomTransition (0x00371290) | 11191 | 1 | certain | 0x0018c4bc | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EControlToSpline (0x003712a8) | 11192 | 1 | certain | 0x0018c4a8 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EControlToPlayer (0x003712c0) | 11193 | 1 | certain | 0x0018c4a4 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EControlToCPU (0x003712d8) | 11194 | 1 | certain | 0x0018c4a0 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ECollision (0x003712f0) | 11195 | 1 | certain | 0x0018c49c | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EClearProgrammerEvent (0x00371308) | 11196 | 1 | certain | 0x0018c498 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EClearBitMagicCounter (0x00371320) | 11197 | 1 | certain | 0x0018c494 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ECinematicCamera (0x00371338) | 11198 | 1 | certain | 0x0018c490 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EChangeStage (0x00371350) | 11199 | 1 | certain | 0x0018c48c | 10.0 | 0/0 | 0.0 | yes | 0.0 |
| EChangeMaxTraffic (0x00371368) | 11200 | 1 | certain | 0x0018c488 | 10.0 | 0/0 | 0.0 | yes | 0.0 |
| EChangeCarCameraView (0x00371380) | 11201 | 1 | certain | 0x0018c484 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ECameraZoom (0x00371398) | 11202 | 1 | certain | 0x0018c480 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ECameraUpdate (0x003713b0) | 11203 | 1 | certain | 0x0018c47c | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ECameraShake (0x003713c8) | 11204 | 1 | certain | 0x0018c478 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ECameraLockOn (0x003713e0) | 11205 | 1 | unresolved | 0x001b3d78 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| ECallStage (0x003713f8) | 11206 | 1 | certain | 0x0018c470 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| ECall911 (0x00371410) | 11207 | 1 | certain | 0x0018c46c | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EButtonMsgOn (0x00371428) | 11208 | 1 | certain | 0x0018c468 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EButtonMsgOff (0x00371440) | 11209 | 1 | certain | 0x0018c464 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EBeginDownloadCode (0x00371458) | 11210 | 1 | unresolved | 0x0018c670 | 1.0 | 1/0 | 0.0 |  | 1.0 |
| EBashCarAlongObjectAxis (0x00371470) | 11211 | 1 | certain | 0x0018c45c | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EAwardKillToPlayer (0x00371488) | 11212 | 1 | certain | 0x0018c458 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EAwardHitToPlayer (0x003714a0) | 11213 | 1 | certain | 0x0018c454 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EAutoDriveSteering (0x003714b8) | 11214 | 1 | certain | 0x0018c450 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EAutoDriveSpeedChange (0x003714d0) | 11215 | 1 | certain | 0x0018c44c | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EAutoDrive (0x003714e8) | 11216 | 1 | certain | 0x0018c448 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EAudioUpdate (0x00371500) | 11217 | 1 | certain | 0x0018c444 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EAnimUpdate (0x00371518) | 11218 | 1 | certain | 0x0018c438 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EAddScoreObjective (0x00371530) | 11219 | 1 | unresolved | 0x001b3d78 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| EAddScoreAction (0x00371548) | 11220 | 1 | certain | 0x0018c414 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EAddObjective (0x00371560) | 11221 | 1 | probable | 0x0018c410 | 1.0 | 1/0 | 0.0 |  | -3.0 |
| EActorMuzzleFlash (0x00371620) | 11222 | 1 | certain | 0x0018c40c | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EActivateAIElement (0x00371638) | 11223 | 1 | certain | 0x0018c3f8 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EAbortCinematic (0x00371650) | 11224 | 1 | certain | 0x0018c3f4 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EAIUpdate (0x00371668) | 11225 | 1 | certain | 0x0018c3f0 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EAIElementSetWakeRange (0x00371680) | 11226 | 1 | certain | 0x0018c3ec | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EAIElementSetVehicleParams (0x00371698) | 11227 | 1 | certain | 0x0018c3e8 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EAIElementSetNonScoreable (0x003716b0) | 11228 | 1 | certain | 0x0018c3e4 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EAIElementSetCharParams (0x003716c8) | 11229 | 1 | certain | 0x0018c3e0 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EAIElementSetAccuracy (0x003716e0) | 11230 | 1 | certain | 0x0018c3dc | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EAIElementFireOn (0x003716f8) | 11231 | 1 | certain | 0x0018c3d8 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EAIElementFireOff (0x00371710) | 11232 | 1 | certain | 0x0018c3d4 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| EAICommand (0x00371728) | 11233 | 1 | certain | 0x0018c3d0 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| E007Logo (0x00371740) | 11234 | 1 | certain | 0x0018c3b0 | 7.0 | 0/1 | 0.0 | yes | 7.0 |
| Event (0x00371758) | 11235 | 1 | unresolved | 0x001b3d78 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| InputDevice (0x00373dd0) | 11243 | 5 | certain | 0x0018d9fc | 5.0 | 3/0 | 1.0 |  | 1.41 |
| PS2PadDevice (0x00374880) | 11246 | 5 | unresolved | 0x001a76fc | 0.13 | 0/0 | 0.06 |  | -0.34 |
| AttributeSystem (0x00374bb8) | 11248 | 3 | certain | 0x0018dc28 | 9.78 | 1/1 | 0.89 | yes | -4.17 |
| Schedule_OncePerGameLoop (0x003775d8) | 11262 | 2 | unresolved | 0x001a3288 | -3.0 | 0/1 | 0.0 |  | -3.0 |
| Schedule_QuarterSimRate (0x003775f8) | 11263 | 2 | unresolved | 0x001a3288 | -3.0 | 0/1 | 0.0 |  | -3.0 |
| Schedule_HalfSimRate (0x00377618) | 11264 | 2 | unresolved | 0x001a3288 | -3.0 | 0/1 | 0.0 |  | -3.0 |
| Schedule_SimRate (0x00377638) | 11265 | 2 | unresolved | 0x001a3288 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| Grenade (0x00377cf8) | 11276 | 7 | certain | 0x0018ed98 | 16.86 | 5/0 | 0.93 | yes | 2.86 |
| Human (0x00377f20) | 11278 | 7 | certain | 0x0018edf4 | 13.92 | 5/1 | 0.96 | yes | -4.41 |
| Mine (0x00378170) | 11280 | 7 | certain | 0x0018ee28 | 12.92 | 4/1 | 0.96 | yes | -1.15 |
| Missile (0x00378748) | 11282 | 7 | certain | 0x0018eef8 | 13.73 | 5/1 | 0.87 | yes | -0.25 |
| Newton (0x003789f0) | 11284 | 7 | certain | 0x0018ef7c | 12.94 | 4/1 | 0.97 | yes | -1.35 |
| PBondCar (0x00379ef0) | 11286 | 84 | certain | 0x0018f580 | 87.93 | 79/1 | 0.96 | yes |  |
| PHelicopter (0x0037a4e8) | 11288 | 7 | certain | 0x0018f8bc | 12.97 | 4/1 | 0.99 | yes | -5.21 |
| PhysicsNamespace (0x0037a6a8) | 11290 | 2 | certain | 0x0018f8fc | 11.0 | 1/0 | 0.0 | yes | -3.0 |
| PhysicsObject (0x0037a8d8) | 11292 | 7 | certain | 0x0018f9a0 | 13.39 | 5/1 | 0.69 | yes | -1.73 |
| PTank (0x0037a9f8) | 11294 | 84 | certain | 0x0018f580 | 70.94 | 79/0 | 0.97 | OTHER |  |
| RayShell (0x0037bfb8) | 11301 | 1 | certain | 0x0018ffe4 | 10.0 | 0/0 | 0.0 | yes | 0.0 |
| Sentry (0x0037c260) | 11303 | 7 | certain | 0x00190034 | 13.93 | 5/1 | 0.97 | yes | -4.33 |
| Shell (0x0037ccf8) | 11305 | 7 | certain | 0x0019006c | 6.87 | 5/0 | 0.93 |  | -7.13 |
| Smackable (0x0037cec0) | 11307 | 7 | certain | 0x00190248 | 13.95 | 5/1 | 0.97 | yes | -4.34 |
| RCamera (0x0037d790) | 11313 | 3 | certain | 0x00190350 | 12.44 | 1/0 | 0.72 | yes | -1.0 |
| EAGLNamespace (0x0037f370) | 11318 | 1 | unresolved | 0x001b3d78 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| GlobalSymbolTable (0x0037f388) | 11319 | 1 | unresolved | 0x001b3d78 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| RFog (0x00380728) | 11324 | 3 | certain | 0x00191514 | 12.95 | 1/0 | 0.97 | yes | -1.63 |
| RLightManager (0x00381608) | 11327 | 3 | certain | 0x00191628 | 9.99 | 1/1 | 0.99 | yes | -4.57 |
| RPlayerCamera (0x00382578) | 11333 | 7 | certain | 0x001918ac | 11.78 | 3/1 | 0.89 | yes | -11.14 |
| RPlayerViewCamera (0x00383278) | 11336 | 6 | certain | 0x00191910 | 15.0 | 3/0 | 1.0 | yes | -7.85 |
| RReflection (0x00383f98) | 11340 | 5 | certain | 0x001926a4 | 12.0 | 0/0 | 1.0 | yes | 0.8 |
| RRenderDebugViewScreenSpace (0x00384ac8) | 11342 | 5 | certain | 0x001919b4 | 13.71 | 2/0 | 0.86 | yes | -4.25 |
| RRenderDebugViewPerspective (0x00384b00) | 11343 | 5 | certain | 0x001919a0 | 12.99 | 1/0 | 0.99 | yes | -2.19 |
| RRenderHUDView (0x00385888) | 11346 | 5 | unresolved | 0x0018d9fc | 0.8 | 0/0 | 0.4 |  | 0.5 |
| RRenderWorldCamera (0x00386430) | 11348 | 6 | certain | 0x00191b2c | 11.86 | 3/1 | 0.93 | yes | -8.0 |
| RSceneObj (0x00386a88) | 11351 | 19 | certain | 0x00191be0 | 15.91 | 7/1 | 0.96 | yes | -4.08 |
| RSkeletalObj (0x00386c68) | 11353 | 19 | certain | 0x00191c78 | 19.94 | 8/0 | 0.97 | yes | -8.41 |
| RTextureContext (0x00387718) | 11359 | 2 | unresolved | 0x001a3288 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| RTextureContextManager (0x00387808) | 11361 | 3 | unresolved | 0x0018bf48 | 1.75 | 0/0 | 0.87 |  | 0.29 |
| RVehicle (0x003885d8) | 11366 | 19 | certain | 0x001922d0 | 17.96 | 9/1 | 0.98 | yes | -30.55 |
| RViewCamera (0x00388a48) | 11368 | 5 | certain | 0x00192444 | 9.69 | 1/1 | 0.85 | yes | -5.2 |
| RWorldCamera (0x00389118) | 11370 | 7 | certain | 0x001924e4 | 11.73 | 3/1 | 0.87 | yes | -10.75 |
| FeatureManager (0x003891b8) | 11372 | 1 | unresolved | 0x001b3d78 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| RColorize (0x0038a7c8) | 11376 | 3 | certain | 0x001927c8 | 11.07 | 2/0 | -0.46 | yes | -4.48 |
| RDecalManager (0x0038b840) | 11379 | 3 | certain | 0x00192838 | 9.85 | 1/1 | 0.93 | yes | -4.0 |
| RFlockManager (0x0038ceb0) | 11383 | 3 | unresolved | 0x0018bf48 | 1.43 | 0/0 | 0.72 |  | 0.79 |
| RGain (0x0038d698) | 11385 | 3 | certain | 0x00192bdc | 8.22 | 1/1 | 0.11 | yes | -4.15 |
| RLensFlareManager (0x0038dee8) | 11387 | 3 | certain | 0x00192bf4 | 7.92 | 1/1 | -0.04 | yes | -4.02 |
| RLightning (0x0038e820) | 11389 | 3 | certain | 0x00192d20 | 10.03 | 2/1 | 0.51 | yes | -7.46 |
| RMissileCam (0x0038f0b8) | 11391 | 3 | certain | 0x00192d38 | 9.98 | 1/1 | 0.99 | yes | -4.52 |
| RMovableParticleSystem (0x00390968) | 11395 | 3 | probable | 0x00192da8 | 2.9 | 1/0 | 0.95 |  | -1.0 |
| RParticleSystem::RParticleCreator (0x003909d0) | 11398 | 1 | unresolved | 0x001b3d78 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| RParticulate (0x003912b8) | 11400 | 3 | certain | 0x00192e54 | 10.0 | 1/1 | 1.0 | yes | -4.63 |
| RPostProcessing (0x00391b78) | 11402 | 3 | certain | 0x00192ec4 | 10.99 | 2/1 | 1.0 | yes | -7.25 |
| RSniperZoom (0x003934a0) | 11409 | 3 | certain | 0x00193008 | 10.94 | 2/1 | 0.97 | yes | -7.52 |
| RWater (0x00393de0) | 11413 | 3 | certain | 0x001930b8 | 12.95 | 1/0 | 0.97 | yes | -1.63 |
| RGlareManager (0x00394f68) | 11417 | 3 | certain | 0x001931e0 | 13.57 | 2/0 | 0.78 | yes | -4.63 |
| SRuleProgCounter (0x00397a28) | 11429 | 2 | probable | 0x00193628 | 1.0 | 1/0 | 0.0 |  | -3.0 |
| SRuleCollision (0x00397a48) | 11430 | 2 | unresolved | 0x001a3288 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| SRulePlayerDir (0x00397a68) | 11431 | 2 | probable | 0x00193618 | 1.0 | 1/0 | 0.0 |  | -3.0 |
| SRuleDifficulty (0x00397a88) | 11432 | 2 | unresolved | 0x001a3288 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| SRuleTimer (0x00397aa8) | 11433 | 2 | probable | 0x00193650 | 1.0 | 1/0 | 0.0 |  | -3.0 |
| SRuleSpeed (0x00397ac8) | 11434 | 2 | unresolved | 0x001a3288 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| SRuleRange (0x00397ae8) | 11435 | 2 | probable | 0x00193640 | 1.0 | 1/0 | 0.0 |  | -3.0 |
| SRuleProg (0x00397b28) | 11437 | 2 | probable | 0x00193600 | 1.0 | 1/0 | 0.0 |  | -3.0 |
| SRulePIP (0x00397b48) | 11438 | 2 | unresolved | 0x001a3288 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| SRuleHealth (0x00397b68) | 11439 | 2 | unresolved | 0x001a3288 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| SRuleDeath (0x00397b88) | 11440 | 2 | unresolved | 0x001a3288 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| SRuleDamage (0x00397ba8) | 11441 | 2 | probable | 0x001935e8 | 1.0 | 1/0 | 0.0 |  | -3.0 |
| SRuleAmmo (0x00397bc8) | 11442 | 2 | probable | 0x00193630 | 1.0 | 1/0 | 0.0 |  | -3.0 |
| SRuleAlways (0x00397be8) | 11443 | 2 | unresolved | 0x001a3288 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| WSound (0x00399d98) | 11479 | 4 | unresolved | 0x00193ae8 | 2.99 | 1/0 | 1.0 |  | 2.99 |
| GiottoNamespace (0x0039bf78) | 11491 | 2 | unresolved | 0x001a3288 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| GGirlieMaterial (0x0039d8e8) | 11495 | 1 | unresolved | 0x001b3d78 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| GOrthoHudView (0x003aae90) | 11497 | 5 | unresolved | 0x0018d9fc | -4.44 | 0/2 | 0.78 |  | -5.84 |
| GHud (0x003aaec8) | 11498 | 1 | certain | 0x001a06c0 | 7.0 | 0/1 | 0.0 | yes | -3.0 |
| GSubtitles (0x003ac0f8) | 11502 | 1 | certain | 0x001a08b8 | 7.0 | 0/1 | 0.0 | yes | -3.0 |
| IniFiles (0x003ac808) | 11505 | 1 | certain | 0x001a0998 | 10.0 | 0/0 | 0.0 | yes | 0.0 |
| EAGLAnim::FnCycle (0x003aea48) | 11579 | 15 | unresolved | 0x001a0f54 | 1.95 | 0/0 | 0.97 |  | 1.08 |
| EAGLAnim::FnPoseMirror (0x003aead0) | 11580 | 15 | certain | 0x001a0f18 | 13.96 | 2/0 | 0.98 | yes | -4.6 |
| EAGLAnim::FnGraft (0x003aeb58) | 11581 | 15 | unresolved | 0x001a0f90 | 1.96 | 0/0 | 0.98 |  | 1.93 |
| EAGLAnim::FnRawLinearChannel (0x003aebe0) | 11582 | 18 | certain | 0x001a0d38 | 12.64 | 4/1 | 0.82 | yes | -10.7 |
| EAGLAnim::FnRawEventChannel (0x003aec80) | 11583 | 18 | certain | 0x001a0cf0 | 12.88 | 4/1 | 0.94 | yes | -10.35 |
| EAGLAnim::FnCompoundChannel (0x003af048) | 11585 | 18 | certain | 0x001a0be8 | 12.92 | 1/0 | 0.96 | yes | -2.65 |
| EAGLAnim::FnRawStateChan (0x003af498) | 11588 | 18 | certain | 0x001a1018 | 13.92 | 2/0 | 0.96 | yes | -1.74 |
| EAGLAnim::FnDeltaQ (0x003af808) | 11591 | 19 | certain | 0x001a1450 | 12.94 | 1/0 | 0.97 | yes | -7.06 |
| EAGLAnim::FnDeltaSingleQ (0x003afae0) | 11593 | 19 | certain | 0x001a1360 | 12.94 | 1/0 | 0.97 | yes | -7.06 |
| EAGLAnim::FnDeltaQFast (0x003afe80) | 11595 | 18 | certain | 0x001a13e0 | 12.84 | 1/0 | 0.92 | yes | 2.15 |
| EAGLAnim::FnAnimMemoryMap (0x003b0080) | 11597 | 18 | certain | 0x001a1130 | 10.69 | 2/1 | 0.84 | yes | -4.14 |
| EAGLAnim::FnAnim (0x003b0618) | 11601 | 15 | unresolved | 0x001a0ea0 | 1.09 | 0/0 | 0.55 |  | 0.82 |
| EAGLAnim::FnDeltaF3 (0x003b0bc0) | 11603 | 18 | certain | 0x001a12e8 | 12.94 | 1/0 | 0.97 | yes | 1.56 |
| EAGLAnim::FnDeltaF1 (0x003b1180) | 11605 | 18 | certain | 0x001a1270 | 9.93 | 1/1 | 0.97 | yes | -1.41 |
| EAGLAnim::FnEventBlender (0x003b1568) | 11608 | 15 | unresolved | 0x001a0f90 | 1.96 | 0/0 | 0.98 |  | 1.91 |
| EAGLAnim::FnKeyQuatChan (0x003b2300) | 11615 | 18 | certain | 0x001a0e10 | 13.95 | 2/0 | 0.98 | yes | -0.86 |
| EAGLAnim::FnKeyLerpChan (0x003b23a0) | 11616 | 18 | certain | 0x001a0dc8 | 13.95 | 2/0 | 0.98 | yes | -0.82 |
| EAGLAnim::FnDeltaQuatChan (0x003b24e0) | 11618 | 18 | certain | 0x001a10a8 | 12.95 | 1/0 | 0.98 | yes | 1.93 |
| EAGLAnim::FnDeltaLerpChan (0x003b2580) | 11619 | 18 | certain | 0x001a1060 | 12.95 | 1/0 | 0.97 | yes | 1.87 |
| EAGLAnim::FnPhaseChan (0x003b29a8) | 11623 | 18 | certain | 0x001a0fd0 | 12.96 | 1/0 | 0.98 | yes | 1.97 |
| EAGLAnim::FnRunBlender (0x003b2de8) | 11625 | 15 | certain | 0x001a1504 | 9.9 | 1/1 | 0.95 | yes | 4.97 |
| EAGLAnim::FnTurnBlender (0x003b3300) | 11627 | 15 | certain | 0x001a14b4 | 12.95 | 1/0 | 0.98 | yes | -2.17 |
| UCarpNamespace (0x003c0fa0) | 11722 | 2 | certain | 0x001a2244 | 7.0 | 0/1 | 0.0 | yes | -3.0 |
| UCharNamespace (0x003c0fc0) | 11723 | 2 | unresolved | 0x001a3288 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| USymbolTable (0x003c0fe0) | 11724 | 1 | certain | 0x001a2240 | 10.0 | 0/0 | 0.0 | yes | 0.0 |
| UGroupRecursiveSort (0x003c19a8) | 11732 | 3 | unresolved | 0x0018bf48 | 2.0 | 0/0 | 1.0 |  | 0.15 |
| UFileFind (0x003c1be0) | 11737 | 1 | unresolved | 0x001b3d78 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| CARP::TagResolver (0x003c1f58) | 11747 | 3 | unresolved | 0x001a20cc | 2.99 | 1/0 | 1.0 |  | 2.95 |
| CARP::SymbolicResolver (0x003c1f80) | 11748 | 3 | certain | 0x001a20cc | 3.99 | 2/0 | 1.0 |  | -0.05 |
| UFileHandler (0x003c7c28) | 11753 | 2 | unresolved | 0x001a3288 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| AWorldSound (0x003c2398) | 11758 | 4 | unresolved | 0x00193ae8 | 2.95 | 1/0 | 0.97 |  | 2.95 |
| AVehicle (0x003c28f8) | 11762 | 9 | certain | 0x001a2680 | 12.89 | 4/1 | 0.94 | yes | -10.62 |
| AUltraLite (0x003c2b08) | 11764 | 10 | certain | 0x001a2bc4 | 11.92 | 3/1 | 0.96 | yes | -8.43 |
| ATrafficVehicle (0x003c2e38) | 11767 | 9 | certain | 0x001a2ed8 | 13.93 | 2/0 | 0.97 | yes | -4.03 |
| ASubmersible (0x003c3258) | 11771 | 10 | certain | 0x001a2d44 | 14.91 | 3/0 | 0.96 | yes | -9.15 |
| AStream (0x003c3850) | 11773 | 4 | certain | 0x001a2910 | 10.97 | 2/1 | 0.98 | yes | -7.17 |
| ASound (0x003c3ea0) | 11776 | 4 | unresolved | 0x001a2a7c | 1.88 | 0/0 | 0.94 |  | 1.78 |
| ASnowMobile (0x003c4118) | 11778 | 9 | certain | 0x001a2c40 | 14.91 | 3/0 | 0.96 | yes | -4.63 |
| ASmackable (0x003c4220) | 11780 | 9 | unresolved | 0x001a2f38 | 2.0 | 0/0 | 1.0 |  | 0.93 |
| ASentry (0x003c4378) | 11782 | 9 | certain | 0x001a2f08 | 14.91 | 3/0 | 0.95 | yes | 0.22 |
| ASceneObj (0x003c4438) | 11784 | 9 | certain | 0x001a2f80 | 3.88 | 2/0 | 0.94 |  | -4.0 |
| APlayerVehicle (0x003c4678) | 11786 | 9 | certain | 0x001a2abc | 13.96 | 2/0 | 0.98 | yes | -4.0 |
| APlayerTank (0x003c47f8) | 11788 | 9 | certain | 0x001a2e28 | 11.97 | 3/1 | 0.98 | yes | -7.92 |
| APlayerHeli (0x003c4960) | 11790 | 9 | certain | 0x001a2b14 | 14.93 | 3/0 | 0.96 | yes | -4.74 |
| AOneShotSound (0x003c4a48) | 11793 | 4 | unresolved | 0x0018c840 | 1.93 | 0/0 | 0.97 |  | 1.88 |
| AMenuSoundPriv (0x003c4ba0) | 11796 | 4 | certain | 0x001a250c | 10.92 | 2/1 | 0.96 | yes | -7.36 |
| ALimitedSound (0x003c4c58) | 11799 | 4 | unresolved | 0x001a2a7c | 1.86 | 0/0 | 0.93 |  | 1.45 |
| AHelicopter (0x003c4df8) | 11802 | 9 | certain | 0x001a2e58 | 12.94 | 4/1 | 0.97 | yes | -10.61 |
| AFingoDeath (0x003c5240) | 11806 | 10 | certain | 0x001a2b60 | 12.95 | 4/1 | 0.98 | yes | -11.68 |
| AEngine (0x003c54b0) | 11809 | 2 | certain | 0x001a3058 | 11.0 | 1/0 | 0.0 | yes | -3.0 |
| ACharacter (0x003c5780) | 11811 | 4 | probable | 0x0018a520 | 2.91 | 1/0 | 0.95 |  | -1.06 |
| ABasic (0x003c5828) | 11813 | 5 | certain | 0x001a3060 | 14.82 | 3/0 | 0.91 | yes | -7.85 |
| ABaseSound (0x003c58b8) | 11815 | 4 | certain | 0x0018a510 | 10.77 | 2/1 | 0.89 | yes | -5.0 |
| ARaceEngine (0x003c5d68) | 11823 | 2 | certain | 0x001a310c | 11.0 | 1/0 | 0.0 | yes | -3.0 |
| EAGL_VD_PS2_SINGLE_BUFFERD (0x003c6f10) | 11831 | 6 | unresolved | 0x0018be30 | 1.15 | 0/0 | 0.57 |  | 0.61 |
| RCMP::DECODER (0x003c6fd0) | 11834 | 1 | unresolved | 0x001b3d78 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| RCMP::RCMP_SYSTEM (0x003c6fe8) | 11835 | 1 | unresolved | 0x001b3d78 | 0.0 | 0/0 | 0.0 |  | 0.0 |
| PS2_SONY_CODEC_INTERNAL (0x003c7058) | 11838 | 6 | unresolved | 0x0018bf58 | 1.09 | 0/0 | 0.54 |  | 0.56 |

## Rejected PS2 addresses

- row 10946 ActWeapon virtual table 0x00367458: entry 0 is not a type_info function (ActWeapon_type_info_function)
- row 11356 RStateManager virtual table 0x003873d0: entry 0 is not a type_info function (RStateManager_type_info_function)
- row 11406 RShadowMap virtual table 0x00392768: entry 0 is not a type_info function (RShadowMap_type_info_function)
