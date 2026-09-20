#ifndef DRIVING_ENGINE_STREAM_H_
#define DRIVING_ENGINE_STREAM_H_

// Keeps a streamed file the host does not have from becoming a stall: the movie player waits for a queued
// file to arrive, and on a disc dump without the movies it waits for ever. See Stream.cpp. Standalone only.
void Inject_Stream(void);

#endif // DRIVING_ENGINE_STREAM_H_
