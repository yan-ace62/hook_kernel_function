SRC = logmain.c  sender.c queue.c conf.c worker.c ../common/common.c
LIB_DIR = ../3rdparty

CFLAGS += -Wall 

ifdef CONF_SRV_PORT
	CFLAGS += -DCONF_SRV_PORT=$(CONF_SRV_PORT)
endif

ifdef DEBUG 
	CFLAGS += -g -DDEBUG -DDEBUG_ALLLOG
endif

all:
	cd $(LIB_DIR)/cJSON && make
	cd $(LIB_DIR)/tinylog && make
	gcc  $(CFLAGS) $(SRC) -std=gnu11 -lcurl -lpthread -I../common \
		$(LIB_DIR)/cJSON/libcjson.a  -I $(LIB_DIR)/cJSON  \
		$(LIB_DIR)/tinylog/libtlog.a -I $(LIB_DIR)/tinylog \
		-o cslogd

clean:
	cd $(LIB_DIR)/cJSON && make clean
	cd $(LIB_DIR)/tinylog && make clean
	rm -rf cslogd

.PHONY: all clean




