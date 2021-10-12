
#include "Frame.h"
#include <stdlib.h>


UnpFrame *insertUnpAfter(UnpFrame *origin, Frame *f) {
    UnpFrame *new = (UnpFrame *) calloc(1, sizeof(UnpFrame));
    new->frame = f;
    new->prec = origin;
    new->next = origin->next;
    origin->next->prec = new;
}


void delete_unp(UnpFrame *unp) {
    unp->next->prec = unp->prec;
    unp->prec->next = unp->next;
    free(unp);
}


UnpFrame *initReplacementList(void) {
    UnpFrame *head = (UnpFrame *) calloc(1, sizeof(UnpFrame));
    head->next = head->prec = head;
    head->frame = NULL;
}

UnpFrame *lastElem(UnpFrame *list) {
    return list->prec;
}

UnpFrame *firstElem(UnpFrame *list) {
    return list->next;
}