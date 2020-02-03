# Set compiler flags I was asked for.
CFLAGS = -std=c89 -Wpedantic

# Set the name the application will be named by.
TARGET = prochess

# Add each object file required by the application.
OBJ = prochess.o lib/board.o lib/communicator.o lib/pawn.o lib/player.o

$(TARGET): $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) -pthread -o $(TARGET)

all: $(TARGET)

# Remove all object files.
clean:
	rm -f *.o lib/*.o $(TARGET) *~

run: $(TARGET)
	./$(TARGET)