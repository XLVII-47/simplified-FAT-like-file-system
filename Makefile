all: main

main: main.cpp
	 g++ -o main -std=c++11 main.cpp 

clean:
	rm main