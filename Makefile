CXX=g++
CXXFLAGS=-c -Wall -DOS_PATH_SEPARATOR="'/'" -std=c++11
INCLUDE=-Iinclude -I../pugixml/src
LDFLAGS=-lstdc++

# https://github.com/zeux/pugixml
EXTSOURCES=../pugixml/src/pugixml.cpp
UTILSOURCES=libutils/Timers.cpp libutils/Static.cpp libutils/String8.cpp libutils/Unicode.cpp libutils/String16.cpp libutils/SharedBuffer.cpp
SOURCES=axml2xml.cpp ResourceTypes.cpp $(UTILSOURCES) $(EXTSOURCES)
OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=axml2xml

.PHONY: all
all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CXX) $(LDFLAGS) $(OBJECTS) -o $@

.cpp.o:
	$(CXX) $(CXXFLAGS) $(INCLUDE) $< -o $@

.PHONY: clean
clean:
	rm -f $(EXECUTABLE) $(OBJECTS)
