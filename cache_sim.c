#include <stdlib.h>
#include <omp.h>
#include "types.h"
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

pthread_rwlock_t bus_lock = PTHREAD_RWLOCK_INITIALIZER;
pthread_mutex_t inst_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mem_lock = PTHREAD_MUTEX_INITIALIZER;

int bus_write = 0;
int *bus_signal;
int num_threads = 2;

void init_memory( byte* memory, int memory_size );

void print_memory( byte* memory, int memory_size );

// Helper function to print the cachelines
void print_cachelines(cache *c, int cache_size);

// Helper function to initialize the cachelines
void initialize_cachelines(cache *c, int cache_size);

// Write back value to memory
void copy_back( byte *memory, byte val, byte addr );

// Fetch value from memory stored at specific address
byte mem_fetch( byte *memory, byte addr );

decoded decode_inst_line(char *buffer);

// Write back modified values in cache to memory
void mem_write_back( byte* memory, cache* c, int cache_size);

// Broadcast message on bus - bus write
void broadcast( icn_bus* bus, int msg_type, int calling_thread, byte address);

void cpu_loop(byte *memory, icn_bus* bus);

// Run the simulation
void run_simulation(int num_threads);

int main(int argc, char *argv[]) {
    if( argc > 1 ) {
        num_threads = atoi(argv[1]);
    }

    omp_set_nested(1);
    run_simulation(num_threads);
    return 0;
}

// Run the simulation
void run_simulation(int num_threads) {
    // Initialize Global memory
    // Let's assume the memory module holds about 24 bytes of data.
    int memory_size = 24;
    byte *memory = (byte *)malloc(sizeof(byte) * memory_size);
    icn_bus *bus = malloc(sizeof(icn_bus));
    init_memory(memory, memory_size);
    print_memory(memory, memory_size);
    bus_signal = malloc(sizeof(int)*num_threads);

    omp_set_num_threads(num_threads); // Set the number of threads before entering the parallel region
    #pragma omp parallel
    {
        cpu_loop(memory, bus);
    }
    printf("\n");
    print_memory(memory, memory_size);
    free(memory);
    free(bus);
}

void init_memory( byte* memory, int memory_size ) {
    for( int i = 0; i < memory_size; i++ )
        *(memory + i ) = 0;
}

void print_memory( byte* memory, int memory_size ) {
    printf("Memory:\n");
    for( int i = 0; i < memory_size; i++ )
        printf("%02d ", i);
    printf("\n");
    for( int i = 0; i < memory_size; i++ )
        printf("%02d ", *(memory + i ));
    printf("\n\n");
}

void print_cachelines(cache *c, int cache_size) {
    for (int i = 0; i < cache_size; i++) {
        cache cacheline = *(c + i);
        printf("Address: %d, State: %d, Value: %d\n", cacheline.address, cacheline.state, cacheline.value);
    }
}

void initialize_cachelines(cache *c, int cache_size) {
    for (int i = 0; i < cache_size; i++) {
        cache cacheline = *(c + i);
        (c + i)->value = 0;
        (c + i)->state = I;
    }
}

void copy_back( byte *memory, byte val, byte addr ) {
    // printf("copyback %d - %d\n", addr, val);

    pthread_mutex_lock(&mem_lock);
    *(memory + addr) = val;
    pthread_mutex_unlock(&mem_lock);
}

byte mem_fetch( byte *memory, byte addr ) {
    // printf("fetch %d - %d\n", addr, *(memory + addr));

    pthread_mutex_lock(&mem_lock);
    byte val = *(memory + addr);
    pthread_mutex_unlock(&mem_lock);
    return val;
}

decoded decode_inst_line(char *buffer) {
    decoded inst;
    char inst_type[3];
    sscanf(buffer, "%s", inst_type);
    if (!strcmp(inst_type, "RD")) {
        int addr = 0;
        sscanf(buffer, "%s %d", inst_type, &addr);
        inst.type = 0;
        inst.value = -1;
        inst.address = addr;
    } else if (!strcmp(inst_type, "WR")) {
        int addr = 0;
        int val = 0;
        sscanf(buffer, "%s %d %d", inst_type, &addr, &val);
        inst.type = 1;
        inst.value = val;
        inst.address = addr;
    }
    return inst;
}

void mem_write_back( byte* memory, cache* c, int cache_size) {
    pthread_mutex_lock(&mem_lock);
    for( int i = 0; i < cache_size; i++ ) {
        cache cacheline = *(c + i);
        if( cacheline.state == M )
        *(memory + cacheline.address) = cacheline.value;
    }
    pthread_mutex_unlock(&mem_lock);
}

void broadcast( icn_bus* bus, int msg_type, int calling_thread, byte address) {
    pthread_rwlock_wrlock(&bus_lock);
    bus->address = address;
    bus->msg = msg_type;
    bus->thread = calling_thread;
    // printf("Broadcasting..thread %d doing %d on %d\n", calling_thread, msg_type, address);
    bus_write = 1;
    for(int i = 0; i < num_threads; i++) {
        bus_signal[i] = 0;
    }
    pthread_rwlock_unlock(&bus_lock);
}

void cpu_loop(byte *memory, icn_bus* bus) {
    // Initialize a CPU level cache that holds about 2 bytes of data.
    int cache_size = 2;
    cache *c = (cache *)malloc(sizeof(cache) * cache_size);

    int thread_num = omp_get_thread_num();
    initialize_cachelines( c, cache_size);
    printf( "Thread %d Running\n", thread_num);
    int processing = 1;
    bus_signal[thread_num] = 0;

    #pragma omp parallel shared(c)
    {
        #pragma omp sections
        {
            #pragma omp section
            {
                char file_name[50];
                sprintf(file_name, "input_%d.txt", thread_num);
                FILE *inst_file = fopen(file_name, "r");
                char inst_line[20];

                // Decode instructions and execute them.
                while (fgets(inst_line, sizeof(inst_line), inst_file)) {

                    decoded inst = decode_inst_line(inst_line);
                    int hash = inst.address % cache_size;

                    pthread_mutex_lock(&inst_lock);
                    cache cacheline = *(c + hash);
                    // printf("Thread %d, insttype = %d, cache address = %d, inst addr = %d,  state = %d\n",
                    //     thread_num, inst.type, cacheline.address, inst.address ,cacheline.state);
                    switch (cacheline.state)
                    {
                        case M:
                                if( cacheline.address != inst.address ) {
                                    copy_back( memory, cacheline.value, cacheline.address );
                                }
                                else break;
                        case S:
                                if( cacheline.address == inst.address && inst.type == 0 )
                                    break;
                        default:
                                broadcast(bus, inst.type, thread_num, inst.address);
                                // printf("Thread %d after sleep\n", thread_num);
                                while(1) {
                                    for(int i = 0; i < num_threads; i++) {
                                        if(bus_signal[i] == 0)
                                            continue;
                                    }
                                    break;
                                }
                                sleep(0.5);
                                cacheline.address = inst.address;
                                cacheline.value = mem_fetch( memory, inst.address);
                                cacheline.state = S;
                                break;
                    }

                    // Modify cache line according to instruction
                    switch (inst.type) {
                        case 0: // Read
                            printf("Thread %d: RD %d: %d\n", thread_num, cacheline.address, cacheline.value);
                            break;
                        case 1: // Write
                            cacheline.value = inst.value;
                            cacheline.state = M;
                            printf("Thread %d: WR %d: %d\n", thread_num, cacheline.address, cacheline.value);
                            break;
                    }

                    // Update cache
                    *(c + hash) = cacheline;

                    pthread_mutex_unlock(&inst_lock);
                }
                processing = 0;
            }
            #pragma omp section
            {
                // Snooping
                // printf("Snooping %d\n", thread_num);
                while( processing )
                {
                    if( bus_write == 1) {
                        bus_signal[thread_num] = 0;
                        // pthread_rwlock_rdlock(&bus_lock);
                        if( thread_num == bus->thread ) {
                            // printf("Thread %d sleeping inside snooper.\n", thread_num);
                            sleep(0.01);
                            bus_signal[thread_num] = 1;
                        }
                        else {
                            int hash = bus->address % cache_size;
                            cache cacheline = *(c + hash);
                            // printf("Thread %d processing bus write- type: %d, address: %d, cacheaddress: %d, cachestate: %d, cachevalue: %d\n", 
                            //         thread_num, bus->msg, bus->address, cacheline.address, cacheline.state, cacheline.value);
                            if( cacheline.address == bus->address ) {

                                switch( cacheline.state )
                                {
                                    case M:
                                            // printf("Snooping thread %d, copyback %d to %d\n", thread_num, cacheline.value, cacheline.address);
                                            if( bus->msg == 0 ) {
                                                copy_back( memory, cacheline.value, cacheline.address);
                                                cacheline.state = S;
                                            }
                                            else
                                                cacheline.state = I;
                                            break;
                                    case S:
                                            // printf("Snooping thread %d, invalidate %d\n", thread_num, cacheline.address);
                                            cacheline.state = I;
                                }
                                *(c + hash) = cacheline;
                            }
                            bus_signal[thread_num] = 1;
                        }
                        // pthread_rwlock_unlock(&bus_lock);
                        bus_write = 0;
                    }
                }
                pthread_rwlock_unlock(&bus_lock);
            }
        }
    }

    pthread_rwlock_wrlock(&bus_lock);
    mem_write_back( memory, c, cache_size);
    pthread_rwlock_unlock(&bus_lock);
    free(c);
}
