#ifndef DICM_SEQUENCE_H
#define DICM_SEQUENCE_H

#include "dicm_item.h"

/* Will either process the item delimiters and return associated token, or
 * return TOKEN_KEY for further processing */
enum token sequence_process_delim(struct level_parser *const self,
                                  const uint32_t tag, const uint32_t ide_vl);
void sequence_setup_level(struct level_parser *const new_item);
void sequence_setup_next_level(const struct level_parser *const self,
                               struct level_parser *const new_item);

void sequence_level_emitter_next_level(const struct level_emitter *const self,
                                       struct level_emitter *const new_item);
void sequence_init_level_emitter(struct level_emitter *const new_item);
enum token sequence_process_delim2(struct level_emitter *const self,
                                   const enum dicm_event_type next,
                                   struct ivr *ivr);

#endif /* DICM_SEQUENCE_H */
