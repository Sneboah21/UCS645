CC     = mpicc
CFLAGS = -O2 -Wall
LFLAGS = -lm

all: ques1 ques2 ques3 ques4 ques5

ques1: ques1.c
	$(CC) $(CFLAGS) -o ques1 ques1.c $(LFLAGS)

ques2: ques2.c
	$(CC) $(CFLAGS) -o ques2 ques2.c $(LFLAGS)

ques3: ques3.c
	$(CC) $(CFLAGS) -o ques3 ques3.c $(LFLAGS)

ques4: ques4.c
	$(CC) $(CFLAGS) -o ques4 ques4.c $(LFLAGS)

ques5: ques5.c
	$(CC) $(CFLAGS) -o ques5 ques5.c $(LFLAGS)

run: all
	mpirun -np 4 ./ques1
	mpirun -np 4 ./ques2
	mpirun -np 4 ./ques3
	mpirun -np 4 ./ques4
	mpirun -np 4 ./ques5

clean:
	rm -f ques1 ques2 ques3 ques4 ques5