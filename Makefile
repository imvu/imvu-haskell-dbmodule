TXTFILES:=$(wildcard *.txt)
SQLFILES:=$(patsubst %.txt, schemas/%.sql, $(TXTFILES))

run:	dbmodule.py $(SQLFILES)
	cp Imvu/Resty/Db/*.hs ${RESTY_ROOT}/lib/resty-base/src/Imvu/Resty/Db/

schemas/%.sql:	%.txt dbmodule.py
	mkdir -p Imvu/Resty/Db
	mkdir -p schemas
	./dbmodule.py $<

clean:
	rm -f Imvu/Resty/Db/* schemas/*
