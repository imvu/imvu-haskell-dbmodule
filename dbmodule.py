#!/usr/bin/env python
import sys
import os
import os.path
import ConfigParser


_stypes = {
    'int': 'BIGINT',
    'uint': 'BIGINT UNSIGNED',
    'string': 'VARCHAR(255)',
    'datetime': 'DATETIME',
    'text': 'MEDIUMTEXT',
    'cid': 'INTEGER UNSIGNED',
    'bool': 'BOOLEAN'
    }

_sqldef = {
    'int': 'NOT NULL DEFAULT 0',
    'uint': 'NOT NULL DEFAULT 0',
    'string': "NOT NULL DEFAULT ''",
    'datetime': "NOT NULL DEFAULT '2000-01-01 00:00:00'",
    'text': "NOT NULL DEFAULT ''",
    'cid': 'NOT NULL DEFAULT 0',
    'bool': 'NOT NULL DEFAULT FALSE'
    }

_hstypes = {
    'int': 'Int64',
    'uint': 'Word64',
    'string': 'Text',
    'datetime': 'UTCTime',
    'text': 'Text',
    'cid': 'CustomerId',
    'bool': 'Bool'
    }

def sqltype(typ):
    global _stypes
    return _stypes[typ]

def sqldefault(typ):
    global _sqldef
    return _sqldef[typ]

def hstype(typ):
    global _hstypes
    return _hstypes[typ]

def toname(x):
    return os.path.splitext(os.path.basename(x))[0].lower()

def parse(fn):
    ini = ConfigParser.SafeConfigParser()
    ini.read(fn)
    # make sure we have the necessary directories
    ini.get('dirs', 'sql')
    ini.get('dirs', 'hs')
    # make sure the data types are legit
    for ct in ini.items('props'):
        if not sqltype(ct[1]):
            print "%s is not a valid type" % (ct[1],)
            sys.exit(1)
    return ini

def generate_schema(ini, name, f):
    f.write("/* SQL FOR %s generated by dbmodule.py */\n" % (name,))
    f.write("DROP TABLE IF EXISTS `%s`;\n" % (name,))
    f.write("CREATE TABLE `%s`(\n" % (name,))
    first = 1
    pk = None
    for ct in ini.items('props'):
        col = ct[0]
        typ = ct[1]
        f.write("    `%s` %s %s,\n" % (col, sqltype(typ), ([sqldefault(typ), "NOT NULL AUTO_INCREMENT"][first])))
        if first:
            pk = ct
            first = 0
    f.write("    `last_modified` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP NOT NULL,\n");
    f.write("    PRIMARY KEY (`%s`),\n" % (ct[0],))
    if ini.has_section('indices'):
        for it in ini.items('indices'):
            f.write("    INDEX `%s` " % (it[0],))
            icols = it[1].split(',')
            first = 1
            for ic in icols:
                f.write("%s`%s`" % ([", ", "("][first], ic))
                first = 0
            f.write("),\n")
    f.write("    INDEX (`last_modified`)\n")
    f.write(") ENGINE=INNODB DEFAULT CHARSET=LATIN1 AUTO_INCREMENT=10001;\n")
    f.write("\n");
    f.close()


def hscolname(cn):
    return ''.join([n.capitalize() for n in cn.split('_')])

def maybeShowCol(ct):
    if ct[1] == 'datetime':
        return 'showDate '
    return ''

def maybeFmapCol(ct):
    if ct[1] == 'datetime':
        return "fmap ((fromJustNote \"%s\") . readDate) $ " % (ct[0],)
    return ""

def generate_hs(ini, ucname, lcname, f):
    idtype = hstype(ini.items('props')[0][1])
    idname = ini.items('props')[0][0]
    tblname = lcname
    f.write("-- Haskell for %s generated by dbmodule.py\n" % (ucname,))
    f.write("module Imvu.Db.%s\n" % (ucname,))
    f.write("    ( list%ss\n" % (ucname,))
    f.write("    , get%s\n" % (ucname,))
    f.write("    , update%s\n" % (ucname,))
    f.write("    , new%s\n" % (ucname,))
    f.write("    , newMulti%ss\n" % (ucname,))
    f.write("    , delete%s\n" % (ucname,))
    f.write("    , deleteMulti%ss\n" % (ucname,))
    f.write("    , %s (..)\n" % (ucname,))
    fnexp = set()
    if ini.has_section('indices'):
        for ic in ini.items('indices'):
            icols = ic[1].split(',')
            prefix = ''
            for col in icols:
                prefix += hscolname(col)
                getName = "get%ssBy%s" % (ucname, prefix)
                if not getName in fnexp:
                    f.write("    , %s\n" % (getName,))
                    fnexp.add(getName)
                deleteName = "delete%ssBy%s" % (ucname, prefix)
                if not deleteName in fnexp:
                    f.write("    , %s\n"% (deleteName,))
                    fnexp.add(deleteName)
    f.write("    ) where\n\n")

    usescid = False
    usestime = False
    usestext = False
    for ct in ini.items('props'):
        if ct[1] == 'cid':
            usescid = True
        if ct[1] == 'datetime':
            usestime = True
        if ct[1] == 'text':
            usestext = True
    if usescid:
        f.write("import Imvu.Customer.Types(CustomerId (..))\n")
    if usestime:
        f.write("import Data.Time.Clock (UTCTime)\n")
        f.write("import Safe (fromJustNote)\n")
        f.write("import Imvu.Time (readDate, showDate)\n")
    if usestext:
        f.write("import Data.Text (Text)\n")
    f.write("import Data.Aeson(FromJSON (..), ToJSON (..), object, withObject, (.:), (.=))\n")
    f.write("import qualified Imvu.World.Database as D\n")
    f.write("import Data.Word (Word64)\n")
    f.write("import qualified Data.Map.Lazy as Map\n")
    f.write("\n\n")

    f.write("data %s = %s\n"% (ucname, ucname))
    first = 1
    for ct in ini.items('props')[1:]:
        f.write("    %s %s :: !%s\n" % ([",", "{"][first], ct[0], hstype(ct[1])))
        first = 0
    f.write("    }\n")
    f.write("    deriving (Show, Read, Ord, Eq)\n\n")

    f.write("instance ToJSON %s where\n" % (ucname,))
    f.write("    toJSON (%s {..}) = object\n"% (ucname,))
    first = 1
    for ct in ini.items('props')[1:]:
        f.write("        %s \"%s\" .= %s%s\n" % ([",", "["][first], ct[0], maybeShowCol(ct), ct[0]))
        first = 0
    f.write("        ]\n\n")
    f.write("instance FromJSON %s where\n" % (ucname,))
    f.write("    parseJSON = withObject \"%s\" $ \\o -> do\n" % (ucname,));
    for ct in ini.items('props')[1:]:
        f.write("        %s <- %so .: \"%s\"\n" % (ct[0], maybeFmapCol(ct), ct[0]))
    f.write("        return $ %s {..}\n\n" % (ucname,))

    f.write("list%ss :: D.SupportsDatabase m => m [%s]\n" % (ucname, idtype))
    f.write("list%ss = do\n" % (ucname,))
    f.write("    ms <- D.getMasterReadShard\n")
    f.write("    rows :: [D.Only %s] <- D.select ms \"%s\" [] [\"%s\"] D.NoFilter [] D.NoLimit\n" % (idtype, tblname, idname))
    f.write("    return $ map D.fromOnly rows\n\n")

    f.write("get%s :: D.SupportsDatabase m => %s -> m (Maybe %s)\n" % (ucname, idtype, ucname))
    f.write("get%s pk = do\n" % (ucname,));
    f.write("    ms <- D.getMasterReadShard\n")
    f.write("    row <- D.get ms pk \"%s\" \"%s\"\n" % (idname, tblname))
    first = 1
    for ct in ini.items('props')[1:]:
        f.write("        %s \"%s\"\n" % ([",","["][first], ct[0]))
        first = 0
    f.write("        ]\n")
    f.write("    case row of\n")
    f.write("        Nothing -> return $ Nothing\n")
    f.write("        Just\n")
    first = 1
    for ct in ini.items('props')[1:]:
        f.write("         %s %s\n" % ([",","("][first], ct[0]))
        first = 0
    f.write("         ) -> return $ Just $ %s {..}\n\n" % (ucname,))

    f.write("update%s :: D.SupportsDatabase m => %s -> %s -> m Bool\n" % (ucname, idtype, ucname))
    f.write("update%s pk (%s {..}) = do\n" % (ucname, ucname))
    f.write("    ms <- D.getMasterWriteShard\n")
    f.write("    let upd = D.UpdateRecord D.InsertReplace $ Map.fromList\n")
    f.write("          [ (\"%s\", D.UpdateValue pk)\n" % (idname,))
    for ct in ini.items('props')[1:]:
        f.write("            , (\"%s\", D.UpdateValue %s)\n" % (ct[0], ct[0]))
    f.write("            ]\n")
    f.write("    D.updateWhere ms \"%s\" upd (D.Equal \"%s\" (D.UpdateValue pk))\n\n" % (tblname, idname))

    f.write("new%s :: D.SupportsDatabase m => %s -> m (Maybe %s)\n" % (ucname, ucname, idtype))
    f.write("new%s (%s {..}) = do\n" % (ucname, ucname))
    f.write("    ms <- D.getMasterWriteShard\n")
    f.write("    let upd = D.UpdateRecord D.InsertReplace $ Map.fromList\n")
    first = 1
    for ct in ini.items('props')[1:]:
        f.write("            %s (\"%s\", D.UpdateValue %s)\n" % ([",","["][first], ct[0], ct[0]))
        first = 0
    f.write("            ]\n")
    f.write("    res <- D.insert ms \"%s\" upd\n" % (tblname,))
    f.write("    case res of\n")
    f.write("        Left _ -> return $ Nothing\n")
    f.write("        Right r -> return $ Just $ fromIntegral $ toInteger r\n\n")

    f.write("-- TODO: this can be generated smarter\n")
    f.write("newMulti%ss :: D.SupportsDatabase m => [%s] -> m [Maybe %s]\n" % (ucname, ucname, idtype))
    f.write("newMulti%ss items = do\n" % (ucname,))
    f.write("    mapM new%s items\n" % (ucname,))
    f.write("\n")

    f.write("delete%s :: D.SupportsDatabase m => %s -> m Bool\n" % (ucname, idtype))
    f.write("delete%s pk = do\n" % (ucname,))
    f.write("    ms <- D.getMasterWriteShard\n")
    f.write("    res <- D.delete ms \"%s\" (D.Equal \"%s\" $ D.UpdateValue pk)\n" % (tblname, idname))
    f.write("    return (res == 1)\n\n")

    f.write("-- TODO: this can be generated smarter\n")
    f.write("deleteMulti%ss :: D.SupportsDatabase m => [%s] -> m Bool\n" % (ucname, idtype))
    f.write("deleteMulti%ss items = do\n" % (ucname,))
    f.write("    r <- mapM delete%s items\n" % (ucname,))
    f.write("    return $ any id r\n")
    f.write("\n")

    fnexp = set()
    coldefs = dict()
    for ct in ini.items('props'):
        coldefs[ct[0]] = ct[1]
    if ini.has_section('indices'):
        for ic in ini.items('indices'):
            icols = ic[1].split(',')
            prefix = ''
            collist = []
            for col in icols:
                collist.append(col)
                prefix += hscolname(col)
                getName = "get%ssBy%s" % (ucname, prefix)
                if not getName in fnexp:
                    f.write("%s :: D.SupportsDatabase m => " % (getName,))
                    fnexp.add(getName)
                    for cc in collist:
                        f.write("%s -> " % (hstype(coldefs[cc]),))
                    f.write("m [(%s, %s)]\n" % (idtype, ucname,))
                    f.write("%s " % (getName,))
                    for cc in collist:
                        f.write("%s' " % (cc,))
                    f.write("= do\n")
                    write_get_by(f, ucname, tblname, prefix, collist, coldefs, idname, idtype, ini)
                deleteName = "delete%ssBy%s" % (ucname, prefix)
                if not deleteName in fnexp:
                    f.write("%s :: D.SupportsDatabase m => "% (deleteName,))
                    fnexp.add(deleteName)
                    for cc in collist:
                        f.write("%s -> " % (hstype(coldefs[cc]),))
                    f.write("m Bool\n")
                    f.write("%s " % (deleteName,))
                    for cc in collist:
                        f.write("%s " % (cc,))
                    f.write("= do\n")
                    write_delete_by(f, ucname, tblname, prefix, collist, coldefs)


def write_get_by(f, ucname, tblname, prefix, collist, coldefs, idname, idtype, ini):
    f.write("    ms <- D.getMasterWriteShard\n")
    f.write("    ret <- D.select ms \"%s\" [] " % (tblname,))
    first = 1
    for ct in ini.items('props'):
        f.write("%s\"%s\"" % ([", ","["][first], ct[0]))
        first = 0
    f.write("] ")
    first = 1
    for col in collist:
        if not first:
            f.write("(D.And ")
        f.write("(D.Equal \"%s\" (D.UpdateValue %s'))" % (col, col))
        first = 0
    f.write(')' * (len(collist)-1))
    f.write(' [] D.NoLimit\n')
    f.write('    return $ map snrk ret\n')
    f.write('  where\n')
    f.write('    snrk ')
    first = 1
    for ct in ini.items('props'):
        f.write('%s%s' % ([", ","("][first], ct[0]))
        first = 0
    f.write(') =\n')
    f.write('        (%s, %s {..})\n' % (idname, ucname))
    f.write('\n')


def write_delete_by(f, ucname, tblname, prefix, collist, coldefs):
    f.write("    ms <- D.getMasterWriteShard\n")
    f.write('    res <- D.delete ms \"%s\" ' % (tblname,))
    first = 1
    for col in collist:
        if not first:
            f.write('(D.And ')
        f.write('(D.Equal \"%s\" (D.UpdateValue %s))' % (col, col))
    f.write(')' * (len(collist)-1))
    f.write('\n')
    f.write('    return $ (res > 0)\n')
    f.write('\n')

def generate(ini, name):
    sqlname = os.path.join(ini.get('dirs', 'sql'), name + ".sql")
    f = open(sqlname, 'wb')
    generate_schema(ini, name, f)
    f.close()
    ucname = hscolname(name)
    hsname = os.path.join(ini.get('dirs', 'hs'), ucname + ".hs")
    f = open(hsname, 'wb')
    generate_hs(ini, ucname, name, f)
    f.close()


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise Exception("Expected exactly one input file name")
    p = parse(sys.argv[1])
    name = toname(sys.argv[1])
    generate(p, name)