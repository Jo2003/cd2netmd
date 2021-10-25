CPP      = g++.exe
CC       = gcc.exe
WINDRES  = windres.exe
OBJ      = main.o AudioCD_Helpers.o CAudioCD.o WinHttpWrapper.o
LINKOBJ  = main.o AudioCD_Helpers.o CAudioCD.o WinHttpWrapper.o
LIBS     = -static -static-libgcc -s -lwinhttp
INCS     = 
CXXINCS  = 
BIN      = cd2netmd.exe
CXXFLAGS = $(CXXINCS) -std=c++17 -Os -W
CFLAGS   = $(INCS) -std=c++17 -Os
DEL      = rm.exe

.PHONY: all all-before all-after clean clean-custom

all: all-before $(BIN) all-after

clean: clean-custom
	${DEL} -rf $(OBJ) $(BIN)

$(BIN): $(OBJ)
	$(CPP) $(LINKOBJ) -o $(BIN) $(LIBS)

main.o: main.cpp
	$(CPP) $(CXXFLAGS) -c main.cpp -o main.o

AudioCD_Helpers.o: AudioCD_Helpers.cpp
	$(CPP) $(CXXFLAGS) -c AudioCD_Helpers.cpp -o AudioCD_Helpers.o 

CAudioCD.o: CAudioCD.cpp
	$(CPP) $(CXXFLAGS) -c CAudioCD.cpp -o CAudioCD.o

WinHttpWrapper.o: WinHttpWrapper.cpp
	$(CPP) $(CXXFLAGS) -c WinHttpWrapper.cpp -o WinHttpWrapper.o
