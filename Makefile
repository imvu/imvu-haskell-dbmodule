TXTFILES:=$(wildcard *.txt)
SQLFILES:=$(patsubst %.txt, schemas/%.sql, $(TXTFILES))

run:	dbmodule.py $(SQLFILES)
	cp Imvu/Db/*.hs ../resty/resty-lib/Imvu/Db/

schemas/%.sql:	%.txt dbmodule.py
	mkdir -p Imvu/Db
	mkdir -p schemas
	./dbmodule.py $<

clean:
	rm -f Imvu/Db/* schemas/*
