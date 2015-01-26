all:
	g++ -std=c++0x verify.cpp -o bin/verify

clean:
	rm bin/verify
