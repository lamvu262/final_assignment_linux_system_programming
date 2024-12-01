#include "gateway.h"


// Define for log events
#define LOG_CONNECTION_OPENED 1
#define LOG_CONNECTION_CLOSED 2
#define LOG_TOO_COLD 3
#define LOG_TOO_HOT 4
#define LOG_INVALID_SENSOR 5
#define LOG_DB_CONNECTED 6
#define LOG_DB_TABLE_CREATED 7
#define LOG_DB_LOST 8
#define LOG_DB_UNABLE_TO_CONNECT 9

SharedData shared_data = {
    .start = 0,
    .end = 0,
    .count = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .not_empty = PTHREAD_COND_INITIALIZER,
    .not_full = PTHREAD_COND_INITIALIZER
};

void enqueue(SensorData data) {
    pthread_mutex_lock(&shared_data.lock);
    while (shared_data.count == SHARED_DATA_CAPACITY) {
        pthread_cond_wait(&shared_data.not_full, &shared_data.lock);
    }
    shared_data.buffer[shared_data.end] = data;
    shared_data.end = (shared_data.end + 1) % SHARED_DATA_CAPACITY;
    shared_data.count++;
    pthread_cond_signal(&shared_data.not_empty);
    pthread_mutex_unlock(&shared_data.lock);
}

SensorData dequeue() {
    pthread_mutex_lock(&shared_data.lock);
    while (shared_data.count == 0) {
        pthread_cond_wait(&shared_data.not_empty, &shared_data.lock);
    }
    SensorData data = shared_data.buffer[shared_data.start];
    shared_data.start = (shared_data.start + 1) % SHARED_DATA_CAPACITY;
    shared_data.count--;
    pthread_cond_signal(&shared_data.not_full);
    pthread_mutex_unlock(&shared_data.lock);
    return data;
}

void *log_process(void *arg) {
    int fifo_fd;
    char log_message[256];
    int sequence_number = 1;

    // Create FIFO if it doesn't exist
    mkfifo(LOG_FIFO, 0666);

    fifo_fd = open(LOG_FIFO, O_RDONLY);

    FILE *log_file = fopen("gateway.log", "a");
    if (!log_file) {
        perror("Error opening log file");
        exit(EXIT_FAILURE);
    }

    while (1) {
        if (read(fifo_fd, log_message, sizeof(log_message)) > 0) {
            time_t now = time(NULL);
            fprintf(log_file, "%d %s %s\n", sequence_number++, ctime(&now), log_message);
            fflush(log_file);
        }
    }
}


void writeLog(int logCode, int sensorNodeID, float value, const char* extra) {
    const char* logMessage = getLogMessage(logCode, sensorNodeID, value, extra);
    int logFd = open(LOG_FIFO, O_WRONLY | O_NONBLOCK);
    if (logFd != -1) {
        write(logFd, logMessage, strlen(logMessage) + 1);
        close(logFd);
    } else {
        perror("Failed to open log FIFO for writing");
    }
}

const char* getLogMessage(int logCode, int sensorNodeID, float value, const char* extra) {
    static char buffer[256];
    switch (logCode) {
        case LOG_CONNECTION_OPENED:
            snprintf(buffer, sizeof(buffer), "A sensor node with %d has opened a new connection", sensorNodeID);
            break;
        case LOG_CONNECTION_CLOSED:
            snprintf(buffer, sizeof(buffer), "The sensor node with %d has closed the connection", sensorNodeID);
            break;
        case LOG_TOO_COLD:
            snprintf(buffer, sizeof(buffer), "The sensor node with %d reports it's too cold (avg temp = %.2f)", sensorNodeID, value);
            break;
        case LOG_TOO_HOT:
            snprintf(buffer, sizeof(buffer), "The sensor node with %d reports it's too hot (avg temp = %.2f)", sensorNodeID, value);
            break;
        case LOG_INVALID_SENSOR:
            snprintf(buffer, sizeof(buffer), "Received sensor data with invalid sensor node ID %d", sensorNodeID);
            break;
        case LOG_DB_CONNECTED:
            snprintf(buffer, sizeof(buffer), "Connection to SQL server established");
            break;
        case LOG_DB_TABLE_CREATED:
            snprintf(buffer, sizeof(buffer), "New table %s created", extra ? extra : "unknown");
            break;
        case LOG_DB_LOST:
            snprintf(buffer, sizeof(buffer), "Connection to SQL server lost");
            break;
        case LOG_DB_UNABLE_TO_CONNECT:
            snprintf(buffer, sizeof(buffer), "Unable to connect to SQL server");
            break;
        default:
            snprintf(buffer, sizeof(buffer), "Unknown log code: %d", logCode);
    }
    return buffer;
}

void *connection_manager(void *arg) {
    int server_fd, client_fd, port = *(int *)arg;
    struct sockaddr_in address;
    char log_event[256];

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    listen(server_fd, MAX_SENSORS);

    while (1) {
        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            perror("Accept failed");
            continue;
        }

        SensorData data = { .sensorNodeID = rand() % 100, .temperature = rand() % 50, .timestamp = time(NULL) };
        enqueue(data);
        writeLog(LOG_CONNECTION_OPENED, data.sensorNodeID, 0, NULL);
        sleep(2);
    }
}

void *data_manager(void *arg) {
    while (1) {
        SensorData data = dequeue();
        float avgTemp = data.temperature; // Simulated running average
        if (avgTemp < 10.0) {
            writeLog(LOG_TOO_COLD, data.sensorNodeID, avgTemp, NULL);
        } else if (avgTemp > 40.0) {
            writeLog(LOG_TOO_HOT, data.sensorNodeID, avgTemp, NULL);
        }
    }
}

void *storage_manager(void *arg) {
    printf("Storage manager started\n");
    sqlite3* db;
    int retry = 0;

    while (retry < 3) {
        if (sqlite3_open("DATABASE", &db) == SQLITE_OK) {
            writeLog(LOG_DB_CONNECTED, 0, 0, NULL);

            char* create_table_query = "CREATE TABLE IF NOT EXISTS SensorData ("
                                       "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                                       "sensorNodeID INTEGER, "
                                       "temperature REAL, "
                                       "timestamp INTEGER)";
            if (sqlite3_exec(db, create_table_query, 0, 0, 0) == SQLITE_OK) {
                writeLog(LOG_DB_TABLE_CREATED, 0, 0, "SensorData");
            }
            break;
        } else {
            writeLog(LOG_DB_UNABLE_TO_CONNECT, 0, 0, NULL);
            retry++;
            sleep(5);
        }
    }

    if (retry == 3) {
        writeLog(LOG_DB_LOST, 0, 0, NULL);
        exit(EXIT_FAILURE);
    }

    while (1) {
        SensorData data = dequeue();
        char insert_query[256];
        snprintf(insert_query, sizeof(insert_query),
                 "INSERT INTO SensorData (sensorNodeID, temperature, timestamp) VALUES (%d, %.2f, %ld)",
                 data.sensorNodeID, data.temperature, data.timestamp);

        if (sqlite3_exec(db, insert_query, 0, 0, 0) != SQLITE_OK) {
            writeLog(LOG_DB_LOST, 0, 0, NULL);
        }
    }

    sqlite3_close(db);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);

    // Start log process
    pid_t log_pid = fork();
    if (log_pid == 0) {
        log_process(NULL);
    }

    // Create threads
    pthread_t conn_thread, data_thread, storage_thread;
    pthread_create(&conn_thread, NULL, connection_manager, &port);
    pthread_create(&data_thread, NULL, data_manager, NULL);
    pthread_create(&storage_thread, NULL, storage_manager, NULL);

    pthread_join(conn_thread, NULL);
    pthread_join(data_thread, NULL);
    pthread_join(storage_thread, NULL);

    return 0;
}
