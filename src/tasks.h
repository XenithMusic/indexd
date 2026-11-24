namespace Task {
    void step(const char* step);
    void start(const char* task);
    void fail();
    void fail(const char* msg,...);
    void warn(const char* msg);
    void succeed();
}