
#include "CommandTokenizer.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static char specials[] = ",:*><=(). \t\n";

#define strbegstr(big, little) ( strncmp( (big), (little), strlen(little) ) == 0 ) 
#define isSep(chr) ( isblank(chr) || (chr) == '\0' || strchr(specials, (chr)) )

struct command newCommand(char *command) {
    struct command nouv;
    nouv.command = command;
    nouv.pos = 0;
    nouv.prevpos = -1;
    return nouv;
};

int nextToken(struct command *com, struct token *tok) {
    com->prevpos = com->pos;
    while(com->command[com->pos] != '\0' && isblank(com->command[com->pos]))
        com->pos++;
    if(com->command[com->pos] == '\0') 
        return tok->type = ENDOFCOMMAND;
    switch(com->command[com->pos]) {
        case '(':  com->pos++; return tok->type = PAREN_OUVR;
        case ')':  com->pos++; return tok->type = PAREN_FERM;
        case ',':  com->pos++; return tok->type = VIRGULE;
        case ':':  com->pos++; return tok->type = DEUX_POINTS;
        case '*':  com->pos++; return tok->type = ETOILE;
        case '.':  com->pos++; return tok->type = POINT;
        case '=':  com->pos++; return tok->type = OPEQ;
        case '>':  case '<':
            if(com->command[com->pos+1] == '>' && com->command[com->pos] == '<') {
                com->pos+=2;
                return tok->type = OPNEQ;
            }
            if(com->command[com->pos+1] == '=') {
                com->pos+=2;
                return tok->type = com->command[(com->pos)-2] == '>' ? OPSUPEQ : OPINFEQ;
            }
            return tok->type = com->command[(com->pos)++] == '>' ? OPSUP : OPINF;
    }
    if(strbegstr(com->command+com->pos, "WHERE")) { //TODO : vérifier qu'il y a un séparateur juste après pour ne pas confondre avec un string valide
        com->pos += strlen("WHERE");
        return tok->type = WHERE;
    } else if (strbegstr(com->command+com->pos, "FROM")) {
        com->pos+=strlen("FROM");
        return tok->type = FROM;
    } else if (strbegstr(com->command+com->pos, "SET")) {
        com->pos+=strlen("SET");
        return tok->type = SET;
    } else if (strbegstr(com->command+com->pos, "KEY")) {
        com->pos+=strlen("KEY");
        return tok->type = KEY;
    } else if (strbegstr(com->command+com->pos, "ORDER")) {
        com->pos+=strlen("ORDER");
        return tok->type = ORDER;
    } else if (strbegstr(com->command+com->pos, "AND")) {
        com->pos+= strlen("AND");
        return tok->type = AND;
    } else if(strbegstr(com->command+com->pos, "OR")) {
        com->pos += strlen("OR");
        return tok->type = OR;
    } else if(strbegstr(com->command+com->pos, "INTO")) {
        com->pos += strlen("INTO");
        return tok->type = INTO;
    } else if(strbegstr(com->command+com->pos, "RECORD")) {
        com->pos += strlen("RECORD");
        return tok->type = RECORD;
    } else if(strbegstr(com->command+com->pos, "int")) {
        com->pos += strlen("int");
        return tok->type = TYPE_INT;
    } else if(strbegstr(com->command+com->pos, "float")) {
        com->pos += strlen("float");
        return tok->type = TYPE_FLOAT;
    } else if(strbegstr(com->command+com->pos, "string")) {
        char *endptr, *start = com->command + com->pos + strlen("string");
        long long res = strtoll(start, &endptr, 10);
        if(endptr != start && isSep(*endptr) && res >= 0) {
            com->pos += endptr - &com->command[com->pos];
            tok->attr.iattr = res;
            return tok->type = TYPE_STRING;
        }
    }
    
    char *endptr;
    long long res = strtoll(com->command+com->pos, &endptr, 0);
    if( *endptr == '.' || tolower(*endptr) == 'e') {
        double dres = strtod(com->command + com->pos, &endptr);
        if(endptr != com->command+com->pos &&  isSep(*endptr)) { // C'est un float valide
            com->pos+= endptr - &com->command[com->pos];
            tok->attr.fattr = dres;
            return tok->type = FLOAT_CONSTANT;
        }
    }
    
    if (endptr != com->command+com->pos && isSep(*endptr)) {
        com->pos+= endptr - &com->command[com->pos];
        tok->attr.iattr  = res;
        return tok->type = INT_CONSTANT;
    }
    else {
        int i;
        for (i=0; i<MAX_ATTR; i++) {
            tok->attr.sattr[i] = com->command[com->pos];
            com->pos++;
            if (strchr(specials,com->command[com->pos]) && strncmp(com->command+com->pos+1, "csv", 3)) {
                tok->attr.sattr[i+1] = '\0';
                return tok->type = NOM_VARIABLE;
            }
        }
        tok->attr.sattr[i] = '\0';
        fprintf(stderr, "Erreur dans la commande \"%s\", le nom \"%s...\" est trop long\n", com->command, tok->attr.sattr);
        return tok->type = INVALID_TOK;
    }
}

void pushTokenBack(struct command *com) {
    if(com->prevpos == -1) {
        fprintf(stderr, "W: [pushTokenBack] commande \"%s\" et pos=%d, quelque chose essaye de restaurer un token plus d'une fois\n", com->command, com->pos);
    }
    com->pos = com->prevpos;
    com->prevpos = -1;
}
