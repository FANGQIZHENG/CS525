# 
# 1. Compiler and Flags
# 
# CC       : The C compiler
# CFLAGS   : Flags passed to the compiler for warnings and C99 compliance
# INCLUDES : Directories for header files
# LDFLAGS  : Additional flags for the linker (empty for now)

CC        = gcc
CFLAGS    = -Wall -Wextra -std=c99 -g
INCLUDES  = -I.        # Look for headers in the current directory
LDFLAGS   =

# 
# 2. Project File Lists
# 
# SRCS     : All .c source files needed to build the test executable
# OBJS     : Corresponding .o object files (automatically derived)
# HEADERS  : All .h header files (for reference / dependency tracking)
# EXEC     : Name of the final executable produced by linking the objects

SRCS      = dberror.c storage_mgr.c test_assign1_1.c
OBJS      = $(SRCS:.c=.o)
HEADERS   = dberror.h storage_mgr.h test_helper.h
EXEC      = test_assign1.exe

# 
# 3. Phony Targets
# 
# .PHONY   : Indicates that these names are not real files but actions

.PHONY: all clean test info

# 
# 4. Default Target: all
# 
# The default rule builds the test executable by compiling all object files
# and linking them into $(EXEC). If any object is out of date, its rule will fire.

all: $(EXEC)
	@echo ">>> Build succeeded: $(EXEC) is up-to-date."

# 
# 5. Linking Rule for Executable
# 
# This rule specifies how to link the object files into the final executable.
# We use $(CC) with $(LDFLAGS) (none extra here).

$(EXEC): $(OBJS)
	@echo "Linking objects into $(EXEC)..."
	$(CC) $(LDFLAGS) -o $@ $(OBJS)
	@echo ">>> Linked: $@"

# 
# 6. Pattern Rule: Compile .c into .o
# 
# Any .c file in SRCS can be compiled into a .o file using this pattern.
# $<  is the first prerequisite (the .c file)
# $@  is the target (the .o file)

%.o: %.c $(HEADERS)
	@echo "Compiling $< → $@"
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# 
# 7. Test Target: run tests
# 
# The 'test' target depends on the executable. Once built, it runs it.
# We print a message before and after execution.

test: $(EXEC)
	@echo ">>> Running $(EXEC) tests..."
	@./$(EXEC)
	@echo ">>> Tests completed."

# 
# 8. Clean Target: remove build artifacts
# 
# Removes all .o files and the executable. Silent if files do not exist.

clean:
	@echo "Cleaning up build artifacts..."
	@rm -f $(OBJS) $(EXEC)
	@echo ">>> Clean completed."

# 
# 9. Info Target: display variables and file lists
# 
# Useful for debugging the Makefile or verifying file names

info:
	@echo "----- Makefile Variables -----"
	@echo "CC:        $(CC)"
	@echo "CFLAGS:    $(CFLAGS)"
	@echo "INCLUDES:  $(INCLUDES)"
	@echo "LDFLAGS:   $(LDFLAGS)"
	@echo ""
	@echo "----- Project Files -----"
	@echo "SRCS:      $(SRCS)"
	@echo "OBJS:      $(OBJS)"
	@echo "HEADERS:   $(HEADERS)"
	@echo "EXEC:      $(EXEC)"
	@echo ""

#
# End of Makefile
#
