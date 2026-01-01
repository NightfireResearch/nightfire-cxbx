
struct Schedule {
    void** vtable;
    Schedule* next;
    void* tasks[8];
    unsigned int taskCount;
};