#ifndef GATEWAY_H
#define GATEWAY_H

#include <pthread.h>
#include <time.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Shared data capacity
#define SHARED_DATA_CAPACITY 100
#define MAX_SENSORS 100     // Define the maximum number of sensors
#define DATABASE "database.db"  // Define your database path
// Shared Data structure
typedef struct {
    int sensorNodeID;
    float temperature;
    time_t timestamp;
} SensorData;

typedef struct {
    SensorData buffer[SHARED_DATA_CAPACITY];
    int start;
    int end;
    int count;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} SharedData;

// Log event types
enum LogEvent {
    LOG_CONNECTION_OPENED = 1,
    LOG_CONNECTION_CLOSED,
    LOG_TOO_COLD,
    LOG_TOO_HOT,
    LOG_INVALID_SENSOR,
    LOG_DB_CONNECTED,
    LOG_DB_TABLE_CREATED,
    LOG_DB_LOST,
    LOG_DB_UNABLE_TO_CONNECT
};

// Log FIFO path
#define LOG_FIFO "/tmp/logFifo"

// Shared Data instance
extern SharedData shared_data;

// Function prototypes
void* connection_manager(void* arg);
void* data_manager(void* arg);
void* storage_manager(void* arg);
void* log_process(void* arg);
void enqueue(SensorData data);
SensorData dequeue();

void writeLog(int logCode, int sensorNodeID, float value, const char* extra);
const char* getLogMessage(int logCode, int sensorNodeID, float value, const char* extra);


#endif // GATEWAY_H
