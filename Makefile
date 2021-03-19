PROJECT=bcm
PROG=$(PROJECT)
TARGET=$(PROG)
SRCS= src
CXX=g++
CXXFLAGS=-Wall --std=c++11
LDFLAGS=-lsais -s
RM=rm -f

all: $(TARGET)

$(SRCS)/$(PROG).o: $(SRCS)/$(PROG).cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $^

$(PROG): $(SRCS)/$(PROG).o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	$(RM) $(SRCS)/*.o $(PROG)
