
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>


bool usescid = false;
bool usestime = false;
bool usestext = false;
char infilename[256];
char tblname[256];
char sqlfname[256];
char haskellfname[256];
char ucname[256];
char colnames[100][256];
char coltypes[100][256];
int ncols = 0;

char indexname[100][256];
char indexcols[100][100][256];
int numindexcols[100];
int numindices;

int colix(char const *c) {
    for (int i = 0; i != ncols; ++i) {
        if (!strcmp(colnames[i], c)) {
            return i;
        }
    }
    fprintf(stderr, "Column not found: %s\n", c);
    exit(1);
}

void strtolower(char *tn) {
    while (*tn) {
        *tn = tolower(*tn);
        ++tn;
    }
}

struct typetable {
    char const *name;
    char const *sql;
    char const *dflt;
    char const *haskell;
};
typetable types[] = {
    { "int",        "BIGINT",           "NOT NULL DEFAULT 0",   "Int64" },
    { "uint",       "BIGINT UNSIGNED",  "NOT NULL DEFAULT 0",   "Word64" },
    { "string",     "VARCHAR(255)",     "NOT NULL DEFAULT ''",  "Text" },
    { "datetime",   "DATETIME",         "NOT NULL DEFAULT '2000-01-01 00:00:00'",
                                                                "UTCTime" },
    { "text",       "MEDIUMTEXT",       "NOT NULL DEFAULT ''",  "Text" },
    { "cid",        "INTEGER UNSIGNED", "NOT NULL DEFAULT 0",   "CustomerId" },
    { "bool",       "BOOLEAN",          "NOT NULL DEFAULT FALSE",
                                                                "Bool" },
};

bool checktype(char const *t) {
    for (size_t i = 0; i != sizeof(types)/sizeof(types[0]); ++i) {
        if (!strcmp(t, types[i].name)) {
            return true;
        }
    }
    return false;
}

char const *sqltype(char const *type) {
    for (size_t i = 0; i != sizeof(types)/sizeof(types[0]); ++i) {
        if (!strcmp(type, types[i].name)) {
            return types[i].sql;
        }
    }
    return "unknown";
}

char const *sqldefault(char const *type) {
    for (size_t i = 0; i != sizeof(types)/sizeof(types[0]); ++i) {
        if (!strcmp(type, types[i].name)) {
            return types[i].dflt;
        }
    }
    return "unknown";
}

char const *haskelltype(char const *type) {
    for (size_t i = 0; i != sizeof(types)/sizeof(types[0]); ++i) {
        if (!strcmp(type, types[i].name)) {
            return types[i].haskell;
        }
    }
    return "unknown";
}

void chopext(char *tblname) {
    char *s = strrchr(tblname, '.');
    if (s) {
        *s = 0;
    }
}

void chop(char *rline) {
    char *x = rline + strlen(rline);
    while (x > rline && isspace(x[-1])) {
        --x;
        *x = 0;
    }
}

void hsident(char *name) {
    char *src = name;
    char *dst = name;
    bool upper = true;
    while (*src) {
        if (upper) {
            *dst = toupper(*src);
            upper = false;
            ++dst;
        } else if (!isalnum(*src)) {
            upper = true;
        } else {
            *dst = tolower(*src);
            ++dst;
            upper = false;
        }
        ++src;
    }
    *dst = 0;
}

int main(int argc, char const *argv[]) {
    if (argc != 2) {
usage:
        fprintf(stderr, "usage: dbmodule name.txt\n\n");
        fprintf(stderr, "Table name comes from file name.\n\n");
        fprintf(stderr, "File is format:\n");
        fprintf(stderr, "# comment\n");
        fprintf(stderr, "[props]\n");
        fprintf(stderr, "name=type\n");
        fprintf(stderr, "name=type\n");
        fprintf(stderr, "...\n\n");
        fprintf(stderr, "first column is primary key auto-increment\n");
        fprintf(stderr, "types are int, uint, string, datetime, text, cid\n");
        exit(1);
    }
    if (strlen(argv[1]) > 64) {
        fprintf(stderr, "too long name; %s\n", argv[1]);
        goto usage;
    }
    strcpy(tblname, argv[1]);
    strtolower(tblname);
    strcpy(infilename, tblname);
    chopext(tblname);
    strcpy(ucname, tblname);
    hsident(ucname);
    sprintf(sqlfname, "%s.sql", tblname);
    sprintf(haskellfname, "%s.hs", ucname);

    FILE *f = fopen(infilename, "rb");
    if (!f) {
        fprintf(stderr, "Could not open: %s\n", infilename);
        return 1;
    }
    char rline[256];
    bool props = false;
    bool dirs = false;
    bool indices = false;
    while (!feof(f)) {
        rline[0] = 0;
        fgets(rline, 256, f);
        if (!rline[0]) {
            break;
        }
        chop(rline);
        if (rline[0] == '#' || !rline[0]) {
            continue;
        }
        if (isspace(rline[0])) {
            fprintf(stderr, "Illegal line starting with space: '%s'\n", rline);
            return 1;
        }
        if (!strcmp(rline, "[props]")) {
            props = true;
            dirs = false;
            indices = false;
        } else if (!strcmp(rline, "[dirs]")) {
            props = false;
            dirs = true;
            indices = false;
        } else if (!strcmp(rline, "[indices]")) {
            props = false;
            dirs = false;
            indices = true;
        } else if (rline[0] == '[') {
            fprintf(stderr, "Syntax error: unknown section: %s\n", rline);
            return 1;
        } else if (props) {
            if (strlen(rline) > 255) {
                fprintf(stderr, "too long name; %s\n", rline);
                return 1;
            }
            char *colon = strchr(rline, '=');
            if (!colon) {
                fprintf(stderr, "type expected; %s\n", rline);
                return 1;
            }
            strncpy(colnames[ncols], rline, colon-rline);
            colnames[ncols][colon-rline] = 0;
            strcpy(coltypes[ncols], colon+1);
            if (!checktype(coltypes[ncols])) {
                fprintf(stderr, "unknown type %s for column %s: %s\n", coltypes[ncols], colnames[ncols], rline);
                return 1;
            }
            if (!strcmp(coltypes[ncols], "cid")) {
                usescid = true;
            }
            if (!strcmp(coltypes[ncols], "datetime")) {
                usestime = true;
            }
            if (!strcmp(coltypes[ncols], "string") || !strcmp(coltypes[ncols], "text")) {
                usestext = true;
            }
            ++ncols;
        } else if (dirs) {
            char *eq = strchr(rline, '=');
            if (!eq) {
                fprintf(stderr, "Expected a directory name: %s\n", rline);
                return 1;
            }
            char dsel[256];
            strncpy(dsel, rline, eq-rline);
            dsel[eq-rline] = 0;
            if (!strcmp(dsel, "sql")) {
                sprintf(sqlfname, "%s/%s.sql", eq+1, tblname);
            } else if (!strcmp(dsel, "hs")) {
                sprintf(haskellfname, "%s/%s.hs", eq+1, ucname);
            } else {
                fprintf(stderr, "unknown directory type: %s\n", rline);
                return 1;
            }
        } else if (indices) {
            char *eq = strchr(rline, '=');
            if (!eq) {
                fprintf(stderr, "Expected an index name: %s\n", rline);
                return 1;
            }
            char iname[256];
            strncpy(iname, rline, eq-rline);
            iname[eq-rline] = 0;
            int icol = 0;
            char *estart = eq+1;
            char cname[256];
            while (*eq) {
                ++eq;
                if (*eq == ',' || *eq == 0) {
                    if (eq == estart) {
                        fprintf(stderr, "Empty column in index '%s': %s\n", iname, rline);
                        return 1;
                    }
                    strncpy(cname, estart, eq-estart);
                    cname[eq-estart] = 0;
                    estart = eq+1;
                    for (int q = 0; q != ncols; ++q) {
                        if (!strcmp(colnames[q], cname)) {
                            goto found;
                        }
                    }
                    fprintf(stderr, "Unknown column '%s' referened in index '%s': %s\n", cname, iname, rline);
                    return 1;
                found:
                    strcpy(indexcols[numindices][numindexcols[numindices]], cname);
                    numindexcols[numindices]++;
                }
            }
            if (numindexcols[numindices] == 0) {
                fprintf(stderr, "Index needs columns: %s\n", iname);
                return 1;
            }
            strcpy(indexname[numindices], iname);
            ++numindices;
        } else {
            fprintf(stderr, "Illegal line outside a section: %s\n", rline);
            return 1;
        }
    }
    fclose(f);

    /* sql */

    char q[256];
    time_t tt;
    time(&tt);
    struct tm t = *localtime(&tt);
    strftime(q, sizeof(q), "%Y-%m-%d %H:%M:%S", &t);
    FILE *ofile = fopen(sqlfname, "wb");
    if (!ofile) {
        fprintf(stderr, "Could not create: %s\n", sqlfname);
        return 2;
    }
    printf("%s\n", sqlfname);
    fprintf(ofile, "/* SQL for %s generated by dbmodule.cpp on %s */\n", tblname, q);
    fprintf(ofile, "DROP TABLE IF EXISTS `%s`;\n", tblname);
    fprintf(ofile, "CREATE TABLE `%s`(\n", tblname);
    for (int i = 0; i != ncols; ++i) {
        fprintf(ofile, "    `%s` %s %s,\n", colnames[i], sqltype(coltypes[i]),
                (i == 0) ? "NOT NULL AUTO_INCREMENT" : sqldefault(coltypes[i]));
    }
    fprintf(ofile, "    `last_modified` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP NOT NULL,\n");
    fprintf(ofile, "    PRIMARY KEY (`%s`),\n", colnames[0]);
    for (int i = 0; i != numindices; ++i) {
        fprintf(ofile, "    INDEX `%s` ", indexname[i]);
        for (int j = 0; j != numindexcols[i]; ++j) {
            fprintf(ofile, "%s`%s`", j == 0 ? "(" : ", ", indexcols[i][j]);
        }
        fprintf(ofile, "),\n");
    }
    fprintf(ofile, "    INDEX(`last_modified`)\n");
    fprintf(ofile, ") ENGINE=INNODB DEFAULT CHARSET=LATIN1 AUTO_INCREMENT=10001;\n");
    fprintf(ofile, "\n\n");
    fclose(ofile);


    /* haskell */

    ofile = fopen(haskellfname, "wb");
    if (!ofile) {
        fprintf(stderr, "Could not create: %s\n", haskellfname);
        return 2;
    }
    printf("%s\n", haskellfname);
    char const *idtype = haskelltype(coltypes[0]);
    strftime(q, sizeof(q), "%Y-%m-%d %H:%M:%S", &t);
    fprintf(ofile, "-- Haskell for %s generated by dbmodule.cpp on %s\n", tblname, q);
    fprintf(ofile, "module Imvu.Db.%s\n", ucname);
    fprintf(ofile, "    ( list%ss\n", ucname);
    fprintf(ofile, "    , get%s\n", ucname);
    fprintf(ofile, "    , update%s\n", ucname);
    fprintf(ofile, "    , new%s\n", ucname);
    fprintf(ofile, "    , delete%s\n", ucname);
    fprintf(ofile, "    , %s (..)\n", ucname);
    for (int i = 0; i != numindices; ++i) {
        char ihname[256];
        strcpy(ihname, indexname[i]);
        hsident(ihname);
        fprintf(ofile, "    , get%sBy%s\n", ucname, ihname);
    }
    fprintf(ofile, "    ) where\n\n");

    if (usescid) {
        fprintf(ofile, "import Imvu.Customer.Types(CustomerId (..))\n");
    }
    if (usestime) {
        fprintf(ofile, "import Data.Time.Clock (UTCTime)\n");
    }
    if (usestext) {
        fprintf(ofile, "import Data.Text (Text)\n");
    }
    fprintf(ofile, "import Data.Aeson(FromJSON (..), ToJSON (..), object, withObject, (.:), (.=))\n");
    fprintf(ofile, "import qualified Imvu.World.Database as D\n");
    fprintf(ofile, "import Data.Word (Word64)\n");
    fprintf(ofile, "import qualified Data.Map.Lazy as Map\n");
    fprintf(ofile, "\n\n");

    fprintf(ofile, "data %s = %s\n", ucname, ucname);
    int mlen = 1;
    for (int i = 1; i != ncols; ++i) {
        if (strlen(colnames[i]) > mlen) {
            mlen = strlen(colnames[i]);
        }
    }
    char fmt[128];
    sprintf(fmt, "    %cs %c-%ds :: !%cs\n", '%', '%', mlen, '%');
    for (int i = 1; i != ncols; ++i) {
        fprintf(ofile, fmt, i == 1 ? "{" : ",", colnames[i], haskelltype(coltypes[i]));
    }
    fprintf(ofile, "    }\n");
    fprintf(ofile, "    deriving (Show, Read, Ord, Eq)\n\n");

    fprintf(ofile, "instance ToJSON %s where\n", ucname);
    fprintf(ofile, "    toJSON (%s {..}) = object\n", ucname);
    for (int i = 1; i != ncols; ++i) {
        fprintf(ofile, "        %s \"%s\" .= %s\n", i == 1 ? "[" : ",", colnames[i], colnames[i]);
    }
    fprintf(ofile, "        ]\n\n");
    fprintf(ofile, "instance FromJSON %s where\n", ucname);
    fprintf(ofile, "    parseJSON = withObject \"%s\" $ \\o -> do\n", ucname);
    for (int i = 1; i != ncols; ++i) {
        fprintf(ofile, "        %s <- o .: \"%s\"\n", colnames[i], colnames[i]);
    }
    fprintf(ofile, "        return $ %s {..}\n\n", ucname);

    fprintf(ofile, "list%ss :: D.SupportsDatabase m => m [%s]\n", ucname, idtype);
    fprintf(ofile, "list%ss = do\n", ucname);
    fprintf(ofile, "    ms <- D.getMasterReadShard\n");
    fprintf(ofile, "    rows :: [D.Only %s] <- D.select ms \"%s\" [] [\"%s\"] D.NoFilter [] D.NoLimit\n", idtype, tblname, colnames[0]);
    fprintf(ofile, "    return $ map D.fromOnly rows\n\n");

    fprintf(ofile, "get%s :: D.SupportsDatabase m => %s -> m (Maybe %s)\n", ucname, idtype, ucname);
    fprintf(ofile, "get%s pk = do\n", ucname);
    fprintf(ofile, "    ms <- D.getMasterReadShard\n");
    fprintf(ofile, "    row <- D.get ms pk \"%s\" \"%s\"\n", colnames[0], tblname);
    for (int i = 1; i != ncols; ++i) {
        fprintf(ofile, "        %s \"%s\"\n", i == 1 ? "[" : ",", colnames[i]);
    }
    fprintf(ofile, "        ]\n");
    fprintf(ofile, "    case row of\n");
    fprintf(ofile, "        Nothing -> return $ Nothing\n");
    fprintf(ofile, "        Just\n");
    for (int i = 1; i != ncols; ++i) {
        fprintf(ofile, "         %s %s\n", i == 1 ? "(" : ",", colnames[i]);
    }
    fprintf(ofile, "         ) -> return $ Just $ %s {..}\n\n", ucname);

    fprintf(ofile, "update%s :: D.SupportsDatabase m => %s -> %s -> m Bool\n", ucname, idtype, ucname);
    fprintf(ofile, "update%s pk (%s {..}) = do\n", ucname, ucname);
    fprintf(ofile, "    ms <- D.getMasterWriteShard\n");
    fprintf(ofile, "    let upd = D.UpdateRecord D.InsertReplace $ Map.fromList\n");
    fprintf(ofile, "          [ (\"%s\", D.UpdateValue pk)\n", colnames[0]);
    for (int i = 1; i != ncols; ++i) {
        fprintf(ofile, "            , (\"%s\", D.UpdateValue %s)\n", colnames[i], colnames[i]);
    }
    fprintf(ofile, "            ]\n");
    fprintf(ofile, "    D.updateWhere ms \"%s\" upd (D.Equal \"%s\" (D.UpdateValue pk))\n\n", tblname, colnames[0]);

    fprintf(ofile, "new%s :: D.SupportsDatabase m => %s -> m (Maybe %s)\n", ucname, ucname, idtype);
    fprintf(ofile, "new%s (%s {..}) = do\n", ucname, ucname);
    fprintf(ofile, "    ms <- D.getMasterWriteShard\n");
    fprintf(ofile, "    let upd = D.UpdateRecord D.InsertReplace $ Map.fromList\n");
    for (int i = 1; i != ncols; ++i) {
        fprintf(ofile, "            %s (\"%s\", D.UpdateValue %s)\n", i == 1 ? "[" : ",", colnames[i], colnames[i]);
    }
    fprintf(ofile, "            ]\n");
    fprintf(ofile, "    res <- D.insert ms \"%s\" upd\n", tblname);
    fprintf(ofile, "    case res of\n");
    fprintf(ofile, "        Left _ -> return $ Nothing\n");
    fprintf(ofile, "        Right r -> return $ Just $ fromIntegral $ toInteger r\n\n");

    fprintf(ofile, "delete%s :: D.SupportsDatabase m => %s -> m Bool\n", ucname, idtype);
    fprintf(ofile, "delete%s pk = do\n", ucname);
    fprintf(ofile, "    ms <- D.getMasterWriteShard\n");
    fprintf(ofile, "    res <- D.delete ms \"%s\" (D.Equal \"%s\" $ D.UpdateValue pk)\n", tblname, colnames[0]);
    fprintf(ofile, "    return (res == 1)\n\n");

    for (int i = 0; i != numindices; ++i) {
        char ihname[256];
        strcpy(ihname, indexname[i]);
        hsident(ihname);
        fprintf(ofile, "get%sBy%s :: D.SupportsDatabase m => ", ucname, ihname);
        for (int j = 0; j != numindexcols[i]; ++j) {
            int cix = colix(indexcols[i][j]);
            fprintf(ofile, "%s -> ", haskelltype(coltypes[cix]));
        }
        fprintf(ofile, "m [(%s, %s)]\n", idtype, ucname);
        fprintf(ofile, "get%sBy%s ", ucname, ihname);
        for (int j = 0; j != numindexcols[i]; ++j) {
            fprintf(ofile, "%s' ", indexcols[i][j]);
        }
        fprintf(ofile, "= do\n");
        fprintf(ofile, "    ms <- D.getMasterWriteShard\n");
        fprintf(ofile, "    ret <- D.select ms \"%s\" [] ", tblname);
        for (int q = 0; q != ncols; ++q) {
            fprintf(ofile, "%s\"%s\"", q == 0 ? "[ " : ", ", colnames[q]);
        }
        fprintf(ofile, "] ");
        for (int q = 0; q != numindexcols[i]; ++q) {
            if (q != numindexcols[i]-1) {
                fprintf(ofile, "(D.And ");
            }
            fprintf(ofile, "(D.Equal \"%s\" (D.UpdateValue %s'))", indexcols[i][q], indexcols[i][q]);
            if (q == numindexcols[i]-1) {
                for (int w = 1; w < numindexcols[i]; ++w) {
                    fprintf(ofile, ")");
                }
            }
        }
        fprintf(ofile, " [] D.NoLimit\n");
        fprintf(ofile, "    return $ map snrk ret\n");
        fprintf(ofile, "  where\n");
        fprintf(ofile, "    snrk ");
        for (int q = 0; q != ncols; ++q) {
            fprintf(ofile, "%s%s", q == 0 ? "(" : ", ", colnames[q]);
        }
        fprintf(ofile, ") =\n");
        fprintf(ofile, "        (%s, %s {..})\n", colnames[0], ucname);
        
        fprintf(ofile, "\n");
    }
    fclose(ofile);

    return 0;
}



