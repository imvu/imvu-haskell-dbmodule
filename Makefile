TXTFILES:=$(wildcard *.txt)
SQLFILES:=$(patsubst %.txt, schemas/%.sql, $(TXTFILES))

run:	dbmodule
	mkdir -p Imvu/Db
	mkdir -p schemas
	for i in $(TXTFILES); do ./dbmodule $$i; done
	cp Imvu/Db/*.hs ../resty/Imvu/Db/

dbmodule:	dbmodule.cpp
	g++ -o dbmodule dbmodule.cpp -g

clean:
	rm -f dbmodule *.o Imvu/Db/* schemas/*
