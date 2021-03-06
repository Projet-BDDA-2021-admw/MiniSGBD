#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "DiskManager/PageId.h"
#include "DiskManager/DiskManager.h"
#include "BufferManager/BufferManager.h"
#include "FileManager.h"
#include "util/endianness.h"


#define FREE_LIST 1
#define FULL_LIST 0
#define LAST_FREE 3
#define LAST_FULL 2
#define NEXT_PAGE 1
#define PREC_PAGE 0

static void insertDataPage(PageId page, PageId header, int where);

static void writePageIdToPageBuffer(PageId pageId, uint8_t* buff, int first){
	buff += PAGEID_SIZE*first;
	writeInt32InBuffer(pageId.FileIdx, buff, params.saveEndianness[0]);
	buff[4] = pageId.PageIdx;
}


static PageId readPageIdFromPageBuffer(uint8_t *buff, uint8_t first){
    PageId res;
    buff += PAGEID_SIZE*first;
    res.FileIdx = readInt32FromBuffer(buff, params.saveEndianness[0]);
    res.PageIdx = buff[4];
    return res;
}


// Fonctions pour marquer (juste après les deux pageId's pointants vers prec et next) si on est sur la liste des pages vides ou celle des pages remplies
static int onWhatList(uint8_t *buff) {
    return buff[PAGEID_SIZE*2];
}
static void markOnList(uint8_t *buff, int whatList) {
    buff[PAGEID_SIZE*2] = whatList;
}


PageId createHeaderPage(void){
    //allocation de la page
    PageId pageId = AllocPage();
    
    //écriture sur la page
    uint8_t *headerPage = GetPage(pageId);
    //écriture du PageId de la page dans elle même (permet de construire une liste chaînée circulaire et ainsi 
    // avoir un accès plus rapide au dernier élément
    writePageIdToPageBuffer(pageId,headerPage,FULL_LIST); // next plein
    writePageIdToPageBuffer(pageId,headerPage,FREE_LIST); // next vide
    writePageIdToPageBuffer(pageId,headerPage,LAST_FULL); // last plein
    writePageIdToPageBuffer(pageId,headerPage,LAST_FREE); // last vide
    //on libère la page qui a été modifiée donc dirty à 1
    FreePage(pageId,1);
    return pageId;
}

static PageId addDataPage(RelationInfo *rel) {
    PageId newPage = AllocPage();
   
    /*uint8_t *headerBuff = GetPage(rel->headerPage);
    PageId lastVide = readPageIdFromPageBuffer(headerBuff, LAST_FREE);
    writePageIdToPageBuffer(newPage, headerBuff, LAST_FREE);
    FreePage(rel->headerPage, 1);
    
    uint8_t *lastVideBuff = GetPage(lastVide);
    PageId nextOfLastVideBuff = readPageIdFromPageBuffer(lastVideBuff, !equalPageId(lastVide,rel->headerPage) ? NEXT_PAGE: FREE_LIST); // Doit valoir rel->headerPage si tout va bien
    writePageIdToPageBuffer(newPage, lastVideBuff, !equalPageId(lastVide, rel->headerPage) ? NEXT_PAGE: FREE_LIST); // si on fait ça avant de lire nextOFLast nextOfLast sera newPage :( 
    FreePage(lastVide, 1);
    
    uint8_t *newPageBuff = GetPage(newPage);
    writePageIdToPageBuffer(lastVide, newPageBuff, PREC_PAGE);
    writePageIdToPageBuffer(nextOfLastVideBuff, newPageBuff, NEXT_PAGE);
    markOnList(newPageBuff, FREE_LIST);
    FreePage(newPage, 1);*/
    
    insertDataPage(newPage, rel->headerPage, LAST_FREE);
    return newPage;
}

/* unlinkDataPage: supprome page de la liste chaînée des pages pleines ou vides (respectivement si from == FULL_LIST ou FREE_LIST) */
static void unlinkDataPage(PageId page, PageId header, int from) {
    uint8_t *pageBuff = GetPage(page);
    PageId next = readPageIdFromPageBuffer(pageBuff, NEXT_PAGE);
    PageId prec = readPageIdFromPageBuffer(pageBuff, PREC_PAGE);
    FreePage(page, 1);
    
    uint8_t *nextb = GetPage(next);
    writePageIdToPageBuffer(prec, nextb, equalPageId(next, header) ? (from == FREE_LIST ? LAST_FREE : LAST_FULL) : PREC_PAGE);
    FreePage(next, 1);
    
    uint8_t *precb = GetPage(prec);
    writePageIdToPageBuffer(next, precb, equalPageId(prec, header) ? from : NEXT_PAGE);
    FreePage(prec, 1);
    
}

static void insertDataPage(PageId page, PageId header, int where) { // where == LAST_FREE OU where == LAST_FULL
    uint8_t *headerBuff = GetPage(header);
    PageId last = readPageIdFromPageBuffer(headerBuff, where);
    writePageIdToPageBuffer(page, headerBuff, where);
    FreePage(header, 1);
    
    uint8_t *pageBuff = GetPage(page);
    writePageIdToPageBuffer(header, pageBuff, NEXT_PAGE);
    writePageIdToPageBuffer(last, pageBuff, PREC_PAGE);
    markOnList(pageBuff, where == LAST_FREE ? FREE_LIST : FULL_LIST);
    FreePage(page, 1);
    
    uint8_t *lastb = GetPage(last);
    writePageIdToPageBuffer(page, lastb, equalPageId(last, header) ? (where==LAST_FREE?FREE_LIST:FULL_LIST) : NEXT_PAGE);
    FreePage(last, 1);
}

static PageId getFreePageId(RelationInfo *rel) {
    uint8_t *headerBuff = GetPage(rel->headerPage);
    PageId free_page = readPageIdFromPageBuffer(headerBuff, 1);
    if (equalPageId(free_page, rel->headerPage))
        free_page = addDataPage(rel);
    FreePage(rel->headerPage, 1);
    return free_page;
}

static Rid writeRecordToDataPage(RelationInfo *rel, Record *r, PageId p) {
    uint8_t *buff = GetPage(p);
    uint8_t *bytemap = rel->byteBufOff + buff;
    uint8_t *slots = bytemap + rel->slotCount;
    uint32_t free_slot;
    Rid rid;
    
    //printf("Writing record to data page <%d, %d>...", p.FileIdx, p.PageIdx);
    
    for(free_slot=0; free_slot < rel->slotCount; free_slot++) {
        if(!bytemap[free_slot]) {
            //printf("Found slot %d! \n", free_slot);
            writeToBuffer(r, slots, free_slot*rel->size);
            int remplie = free_slot == rel->slotCount-1;
            if(!remplie) {
                remplie = 1;
                for(int next_free=free_slot+1; next_free < rel->slotCount; next_free++)
                    if(!bytemap[next_free]) 
                        { remplie = 0; break; }
            }
            if (remplie) {
                unlinkDataPage(p, rel->headerPage, FREE_LIST);
                insertDataPage(p, rel->headerPage, LAST_FULL);
            }
            FreePage(p, 1);
            rid.pageId = p;
            rid.slotIdx = free_slot;
            r->rid = rid;
            bytemap[free_slot] = 1;
            return rid;
        }
    }
    fprintf(stderr, "E: [FileManager] Trying to writeRecord to full DataPage (page=<%u,%hhu>)\n", p.FileIdx, p.PageIdx);
    exit(-1);
}

static uint32_t getRecordsInDataPage(RelationInfo *rel, PageId p, Record **list, uint32_t *size, uint32_t *offset) {
    uint32_t readrecs = 0;
    uint8_t *pb = GetPage(p);
    uint8_t *bytemap = pb + rel->byteBufOff;
    uint8_t *slots = bytemap + rel->slotCount;
    for (uint32_t slot = 0; slot < rel->slotCount; slot++) {
        if (bytemap[slot]) {
            if(*offset == *size) {
                *list = (Record *) realloc(*list, sizeof(Record)*((*size)+=2*rel->slotCount));
            }
            Rid rid; rid.pageId = p; rid.slotIdx = slot;
            RecordInit(&(*list)[*offset], rel);
            (*list)[*offset].rid = rid;
            readFromBuffer((*list)+((*offset)++), slots, slot*rel->size);
            readrecs++;
        }
    }
    FreePage(p, 0);
    return readrecs;
}



Rid InsertRecordIntoRelation(RelationInfo *rel, Record *rec) {
    PageId page = getFreePageId(rel);
    //printf("[InsertRecordIntoRelation] Got free page: <%d,%d>. Showing HeapFileList state:\n", page.FileIdx, page.PageIdx);
    //printHeapFileList(rel->headerPage);
    Rid rid =  writeRecordToDataPage(rel, rec, page);
    for (int i=0; i<rel->nbCol; i++) {
        if (rel->indexes[i]) {
            int32_t key = *(int32_t*)getAtColumn(rec, i);
            insertRID(&rel->indexes[i], rid, key);
        }
    }
    return rid;
}

void DeleteRecordFromRelation(RelationInfo *rel, Rid rid) {
    uint8_t *recordPage = GetPage(rid.pageId);
    uint8_t *slotByte = recordPage + rel->byteBufOff + rid.slotIdx;
    if(*slotByte == 0) {
        printf("E: [DeleteRecordFromRelation] Attempting to delete free slot (Rid = <<%d, %d>, %d>)\n", rid.pageId.FileIdx, rid.pageId.PageIdx, rid.slotIdx);
        exit(-1);
    }
    Record rec;
    RecordInit(&rec, rel);
    readFromBuffer(&rec, recordPage + rel->firstSlotOff + rel->size*rid.slotIdx, 0);
    for (int i=0; i<rel->nbCol; i++) {
        if (rel->indexes[i]) {
            int32_t key = *(int32_t*)getAtColumn(&rec, i);
            deleteRID(&rel->indexes[i], rid, key);
        }
    }
    *slotByte = 0;
    int whatList = onWhatList(recordPage);
    FreePage(rid.pageId, 1);
    if(whatList == FULL_LIST) {
        unlinkDataPage(rid.pageId, rel->headerPage, FULL_LIST);
        insertDataPage(rid.pageId, rel->headerPage, LAST_FREE);
    }
}

void UpdateRecord(Record *record) {
    uint8_t *recordPageBuffer = GetPage(record->rid.pageId);
    uint8_t *slotByte = record->relInfo->byteBufOff + recordPageBuffer;
    uint8_t *recordSlot = record->relInfo->firstSlotOff + record->relInfo->size*record->rid.slotIdx + recordPageBuffer;
    if(*slotByte == 0) {
        printf("E: [UpdateRecord] Attempting to update empty slot. (Relation=%s, Rid = <<%d, %d>, %d>)\n", record->relInfo->name, record->rid.pageId.FileIdx, record->rid.pageId.PageIdx, record->rid.slotIdx);
        exit(-1);
    }
    writeToBuffer(record, recordSlot, 0);
    FreePage(record->rid.pageId, 1);
}
                                          
TabDeRecords GetAllRecords(RelationInfo *rel) {
    uint32_t localsize, *size = &localsize;
    uint8_t *header = GetPage(rel->headerPage);
    Record *list = (Record *) malloc(sizeof(Record)*(*size = 2*rel->slotCount));
    uint32_t offset = 0;
    PageId next_full = readPageIdFromPageBuffer(header, FULL_LIST);
    while(!equalPageId(next_full, rel->headerPage)) {
        PageId old_full = next_full;
        uint8_t *pbuff = GetPage(next_full);
        getRecordsInDataPage(rel, next_full, &list, size, &offset); // &list et pas list parce qu'on fait realloc(list)
        next_full = readPageIdFromPageBuffer(pbuff, NEXT_PAGE);
        FreePage(old_full, 0);
    }
    PageId next_free = readPageIdFromPageBuffer(header, FREE_LIST);
    while(!equalPageId(next_free, rel->headerPage)) {
        PageId old_free = next_free;
        uint8_t *pbuff = GetPage(next_free);
        getRecordsInDataPage(rel, next_free, &list, size, &offset);
        next_free = readPageIdFromPageBuffer(pbuff, NEXT_PAGE);
        FreePage(old_free, 0);
    }
    list = (Record *) realloc(list, sizeof(Record)*offset);
    *size = offset;
    TabDeRecords resultat;
    resultat.tab = list;
    resultat.nelems = resultat.maxelems = offset;
    resultat.increment = 5;
    FreePage(rel->headerPage, 0);
    return resultat;
}


ListRecordsIterator *GetListRecordsIterator(RelationInfo *rel) {
    ListRecordsIterator *iter = (ListRecordsIterator *) malloc(sizeof *iter);
    iter->rel = rel;
    uint8_t *header = GetPage(rel->headerPage);
    PageId nextFull = readPageIdFromPageBuffer(header, FULL_LIST), nextFree;
    if (!equalPageId(nextFull, rel->headerPage)) {
        setRecIterState(iter, FULL_LIST, nextFull, GetPage(nextFull), -1);
    } else if (!equalPageId(nextFree = readPageIdFromPageBuffer(header, FREE_LIST), rel->headerPage)) {
        setRecIterState(iter, FREE_LIST, nextFree, GetPage(nextFree), -1);
    } else {
        iter->currentList = -1;
    }
    FreePage(rel->headerPage, 0);
    return iter; // 08-12-2021 https://stackoverflow.com/questions/4644860/function-returns-value-without-return-statement
}

static void incrementIter(ListRecordsIterator *iter) {
    iter->currentSlot++;
    
    if(iter->currentList == FULL_LIST) {
        if (iter->currentSlot < iter->rel->slotCount) {
            //iter->currentSlot++;
            return;
        } else {
            PageId next = readPageIdFromPageBuffer(iter->buffer, NEXT_PAGE);
            FreePage(iter->currentPage, 0);
            if (equalPageId(next, iter->rel->headerPage)) {
                uint8_t *header = GetPage(iter->rel->headerPage);
                PageId firstFree = readPageIdFromPageBuffer(header, FREE_LIST);
                FreePage(iter->rel->headerPage, 0);
                if(equalPageId(firstFree, iter->rel->headerPage)) {
                    iter->currentList = -1;
                    return;
                }
                setRecIterState(iter, FREE_LIST, firstFree, GetPage(firstFree), 0);
            } else {
                setRecIterState(iter, FULL_LIST, next, GetPage(next), 0);
            }
        }
    }
    
    if(iter->currentList == FREE_LIST) {
        while (iter->currentList == FREE_LIST) {
            uint8_t *bytebuf = iter->buffer + iter->rel->byteBufOff; // ??? check me maybe i'm a bug
            while(iter->currentSlot < iter->rel->slotCount) {
                if(bytebuf[iter->currentSlot])
                    return;
                iter->currentSlot++;
            }
            PageId next = readPageIdFromPageBuffer(iter->buffer, NEXT_PAGE);
            FreePage(iter->currentPage, 0);
            if (equalPageId(next,iter->rel->headerPage)) {
                iter->currentList = -1;
            } else {
                setRecIterState(iter, FREE_LIST, next, GetPage(next), 0);
            }
        }
    }
}

Record *GetNextRecord(ListRecordsIterator *iter) {
    Record *rec;
    incrementIter(iter);
    if(iter->currentList == -1)
        return NULL;
    rec = (Record *) malloc(sizeof(Record));
    RecordInit(rec, iter->rel);
    readFromBuffer(rec, iter->buffer, iter->rel->firstSlotOff + iter->currentSlot * iter->rel->size);
    rec->relInfo = iter->rel;
    rec->rid.pageId = iter->currentPage;
    rec->rid.slotIdx = iter->currentSlot;
    return rec;
}


PageIterator GetPageIterator(RelationInfo *rel) {
    PageIterator iter;
    iter.rel = rel;
    uint8_t *headerBuffer = GetPage(rel->headerPage);
    PageId nextFull = readPageIdFromPageBuffer(headerBuffer, FULL_LIST), nextFree = readPageIdFromPageBuffer(headerBuffer, FREE_LIST);
    FreePage(rel->headerPage, 0);
    if (!equalPageId(nextFull, rel->headerPage)) {
      iter.currentPage = nextFull;
      iter.currentList = FULL_LIST;
      iter.buffer = NULL;
    } else if (!equalPageId(nextFree, rel->headerPage)) {
        iter.currentPage = nextFree;
        iter.currentList = FREE_LIST;
        iter.buffer = NULL;
    } else {
        iter.currentList = -1;
    }
    return iter;
}

uint8_t *GetNextPage(PageIterator *iter) {
    if(iter->currentList == -1)
        return NULL;
    PageId next;
    if(iter->buffer) {
        next = readPageIdFromPageBuffer(iter->buffer, NEXT_PAGE);
        FreePage(iter->currentPage, 0);
    } else { // Première fois
        next = iter->currentPage;
    }
    if (equalPageId(next, iter->rel->headerPage)) { // Fin de la liste
        if(iter->currentList == FULL_LIST) { // Fin de la liste des pleines, on passe aux non pleines
            uint8_t *header = GetPage(iter->rel->headerPage);
            PageId firstFree = readPageIdFromPageBuffer(header, FREE_LIST);
            FreePage(iter->rel->headerPage, 0);
            if(equalPageId(firstFree, iter->rel->headerPage)) { // il n'y a aucune non pleine, c'est fini
                iter->currentList = -1;
                return NULL;
            }
            iter->currentList = FREE_LIST;
            iter->currentPage = firstFree;
            iter->buffer = GetPage(firstFree);
            return iter->buffer;
        } else { // Fin de la liste de non pleines, c'est fini
            iter->currentList = -1;
            return NULL;
        }
    } else { // Il reste des pages dans la liste, on charge la prochaine et on la retourne
        iter->buffer = GetPage(next);
        iter->currentPage = next;
        return iter->buffer;
    }
}

RecordsOnPageIterator GetRecordsOnPageIterator(RelationInfo *rel, uint8_t *buffer) {
    RecordsOnPageIterator iter;
    iter.rel = rel;
    iter.pageBuffer = buffer;
    iter.currentSlot = -1;
    iter.record = calloc(1, sizeof(Record));
    RecordInit(iter.record, rel);
    return iter;
}

Record *GetNextRecordOnPage(RecordsOnPageIterator *iter) {
    iter->currentSlot++;
    if(iter->currentSlot >= iter->rel->slotCount)
        return NULL;
    uint8_t *bytemap = iter->pageBuffer + iter->rel->byteBufOff;
    while(iter->currentSlot < iter->rel->slotCount) {
        if (bytemap[iter->currentSlot]) {
            readFromBuffer(iter->record, iter->pageBuffer, iter->rel->firstSlotOff + iter->rel->size*iter->currentSlot);
            return iter->record;
        }
        iter->currentSlot++;
    }
    RecordFinish(iter->record);
    return NULL;
}

void printHeapFileList(PageId headerPage) {
    
    uint8_t *headerBuffer = GetPage(headerPage);
    PageId lastFreePage = readPageIdFromPageBuffer(headerBuffer, LAST_FREE);
    PageId lastFullPage = readPageIdFromPageBuffer(headerBuffer, LAST_FULL);
    PageId freeListPage = readPageIdFromPageBuffer(headerBuffer, FREE_LIST);
    PageId fullListPage = readPageIdFromPageBuffer(headerBuffer, FULL_LIST);
    FreePage(headerPage, 0);
    
    printf("HeaderPage: <%d, %d> [ FreeList = <%d, %d>, LastFree = <%d, %d>, FullList = <%d, %d>, LastFull = <%d, %d> ]\n",
            headerPage.FileIdx, headerPage.PageIdx,
            freeListPage.FileIdx, freeListPage.PageIdx, lastFreePage.FileIdx, lastFreePage.PageIdx,
            fullListPage.FileIdx, fullListPage.PageIdx, lastFullPage.FileIdx, lastFullPage.PageIdx );
    
    printf("FreeList: <%d,%d> ", headerPage.FileIdx, headerPage.PageIdx);
    for (PageId oldFree = headerPage; !equalPageId(freeListPage, headerPage); ) {
        uint8_t *freePageBuffer = GetPage(freeListPage);
        PageId prec = readPageIdFromPageBuffer(freePageBuffer, PREC_PAGE);
        PageId next = readPageIdFromPageBuffer(freePageBuffer, NEXT_PAGE);
        FreePage(freeListPage, 0);
        if(!equalPageId(prec, oldFree)) {
            printf("E: [printHeapFileList] current free page (<%d, %d>) prec (<%d,%d>) not equal to real prec(<%d, %d>).\n", 
                freeListPage.FileIdx, freeListPage.PageIdx, prec.FileIdx, prec.PageIdx, oldFree.FileIdx, oldFree.PageIdx);
            return;
        } else {
            printf(" --> <%d, %d> ", freeListPage.FileIdx, freeListPage.PageIdx);
        }
        oldFree = freeListPage;
        freeListPage = next;
        fflush(stdout);
    } printf("\n");
    
    printf("FullList: <%d,%d> ", headerPage.FileIdx, headerPage.PageIdx);
    for (PageId oldFull = headerPage; !equalPageId(fullListPage, headerPage); ) {
        uint8_t *fullPageBuffer = GetPage(fullListPage);
        PageId prec = readPageIdFromPageBuffer(fullPageBuffer, PREC_PAGE);
        PageId next = readPageIdFromPageBuffer(fullPageBuffer, NEXT_PAGE);
        FreePage(fullListPage, 0);
        if(!equalPageId(prec, oldFull)) {
            printf("E: [printHeapFileList] current full page (<%d, %d>) prec (<%d,%d>) not equal to real prec(<%d, %d>).\n", 
                fullListPage.FileIdx, fullListPage.PageIdx, prec.FileIdx, prec.PageIdx, oldFull.FileIdx, oldFull.PageIdx);
            return;
        } else {
            printf(" --> <%d, %d> ", fullListPage.FileIdx, fullListPage.PageIdx);
        }
        oldFull = fullListPage;
        fullListPage = next;
        fflush(stdout);
    } printf("\n");
}

/*Supprime toutes les pages d'une relation */
void deleteHeapFile(PageId headerPage) {
    uint8_t *headerBuffer = GetPage(headerPage);
    PageId lists[2];
    lists[0] = readPageIdFromPageBuffer(headerBuffer, FREE_LIST), lists[1] = readPageIdFromPageBuffer(headerBuffer,FULL_LIST);
    FreePage(headerPage,0);
    for(int i=0; i<2;i++) // la liste des dispos pour i=0 et la liste des pleines pour i=1
        while (!equalPageId(lists[i], headerPage)) {
            PageId previous = lists[i];
            uint8_t *previousBuf = GetPage(previous);
            lists[i] = readPageIdFromPageBuffer(previousBuf, NEXT_PAGE);
            FreePage(previous,0);
            DesallocPage(previous);
        }
    DesallocPage(headerPage);
}


void createIndex(RelationInfo *rel, int column, int32_t order) {
    rel->indexes[column] = newBPlusNode(order);
    ListRecordsIterator *iter = GetListRecordsIterator(rel);
    Record *rec;
    int i=0;
    while ( rec = GetNextRecord(iter) ) {
        int32_t key = *(int32_t*)getAtColumn(rec, column);
        //printf("Before Inserting Record With Key = %d\n", key);
        // printTree(rel->indexes[column], 0); printf("\n");
        insertRID(&(rel->indexes[column]), rec->rid, key);
        i++;
        //printf("After Inserting Record With Key = %d\n", key);
        //printTree(rel->indexes[column], 0);
        //printf("\n\n");
    }
}

TabDeRecords getRecordsUsingIndex(RelationInfo *rel, int index_column, int32_t key) {
    ListeDeRids rids = getEntryByKey(rel->indexes[index_column], key);
    return getRecordsTabFromRIDList(rids, rel);
}

TabDeRecords getRecordsTabFromRIDList(ListeDeRids liste,RelationInfo *rel){
    TabDeRecords records;
    initArray(records, liste.nelems);
    for(int i=0; i<liste.nelems; i++) {
        Record rec;
        RecordInit(&rec, rel);
        rec.rid = liste.tab[i];
        uint8_t *pageBuffer = GetPage(rec.rid.pageId);
        uint8_t *slotAdress = pageBuffer + rel->firstSlotOff + rel->size*rec.rid.slotIdx;
        readFromBuffer(&rec, slotAdress, 0);
        addElem(records, rec);
        FreePage(rec.rid.pageId, 1);
    }
    return records;
}
