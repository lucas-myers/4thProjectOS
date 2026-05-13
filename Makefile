CXX = g++
CXXFLAGS = -Wall -g

.SUFFIXES: .cpp .o

.cpp.o:
	$(CXX) $(CXXFLAGS) -c $<

all: oss worker

oss: oss.o
	$(CXX) $(CXXFLAGS) -o oss oss.o

worker: worker.o
	$(CXX) $(CXXFLAGS) -o worker worker.o

clean:
	rm -f oss worker *.o *.log