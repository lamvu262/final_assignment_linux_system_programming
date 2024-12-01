all:
	gcc -pthread -o gateway gateway.c -lsqlite3
clean:
	rm gateway
