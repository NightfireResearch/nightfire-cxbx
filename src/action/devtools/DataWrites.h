#ifndef ACTION_DEVTOOLS_DATAWRITES_H_
#define ACTION_DEVTOOLS_DATAWRITES_H_

// Which of the game's data changes in a run, for telling its constant tables from its state
// (docs/action-standalone.md). NIGHTFIRE_DATA_WRITES=<file> turns it on: DataWrites_Init copies .rdata and .data
// (0x0015d160-0x00300cc8) as Inject finds them, and DataWrites_Frame compares them with the copy at the end of every
// frame. The file lists each run of changed bytes with the first frame it was seen changed, rewritten every 600
// frames and at exit; tools/data_writes.py reads it.
void DataWrites_Init(void);
void DataWrites_Frame(void);

#endif // ACTION_DEVTOOLS_DATAWRITES_H_
