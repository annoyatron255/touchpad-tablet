CC		:= gcc
LIBS		:= -levdev -lX11 -lXtst

TARGET		:= touchpad-tablet
BUILD		:= build

.PHONY:		all clean

all:	$(BUILD) $(BUILD)/$(TARGET)

clean:
	@rm -rf $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@

$(BUILD)/$(TARGET):		main.c
	@$(CC) -o $@ *.c $(LIBS)
