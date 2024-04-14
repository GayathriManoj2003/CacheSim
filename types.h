typedef char byte;

struct decoded_inst {
    int type;     // 0 is RD, 1 is WR
    byte address;
    byte value;   // Only used for WR
};

typedef struct decoded_inst decoded;

typedef struct icn_bus {
    int msg;
    int thread;
    byte address;
} icn_bus ;

struct cache {
    byte address; // This is the address in memory.
    byte value;   // This is the value stored in cached memory.
    byte state;   // State according to MESI protocol
};

typedef struct cache cache;

enum cacheline_state { M, E, S, I };