#目标可执行文件名
TARGET = app

#对象文件
OBJS = main.o threadpool.o

#默认目标
all: $(TARGET)

#链接生成可执行文件
$(TARGET): $(OBJS)
	gcc $(OBJS) -lpthread -o $(TARGET)	

#编译规则
main.o: main.c threadpool.h
	gcc -c main.c -o main.o

threadpool.o: threadpool.c threadpool.h
	gcc -c threadpool.c -o threadpool.o

#清理目标
clean:
	rm -f $(OBJS) $(TARGET)

#伪目标
.PHONY: all clean