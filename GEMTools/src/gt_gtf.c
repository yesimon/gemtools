#include "gt_gtf.h"

GT_INLINE gt_gtf_entry* gt_gtf_entry_new(const uint64_t start, const uint64_t end, const gt_strand strand, gt_string* const type){
  gt_gtf_entry* entry = malloc(sizeof(gt_gtf_entry));
  entry->start = start;
  entry->end = end;
  entry->type = type;
  entry->strand = strand;
  return entry;
}
GT_INLINE void gt_gtf_entry_delete(gt_gtf_entry* const entry){
  free(entry);
}

GT_INLINE gt_gtf_ref* gt_gtf_ref_new(void){
  gt_gtf_ref* ref = malloc(sizeof(gt_gtf_ref));
  ref->entries = gt_vector_new(GTF_DEFAULT_ENTRIES, sizeof(gt_gtf_entry*));
  return ref;
}
GT_INLINE void gt_gtf_ref_delete(gt_gtf_ref* const ref){
  register uint64_t s = gt_vector_get_used(ref->entries);
  register uint64_t i = 0;
  for(i=0; i<s; i++){
    gt_gtf_entry_delete( (gt_vector_get_elm(ref->entries, i, gt_gtf_entry)));
  }
  gt_vector_delete(ref->entries);
  free(ref);
}

GT_INLINE gt_gtf* gt_gtf_new(void){
  gt_gtf* gtf = malloc(sizeof(gt_gtf));
  gtf->refs = gt_shash_new();
  gtf->types = gt_shash_new();
  gtf->gene_ids = gt_shash_new();
  gtf->gene_types = gt_shash_new();
  return gtf;
}
GT_INLINE void gt_gtf_delete(gt_gtf* const gtf){
  gt_shash_delete(gtf->refs, true);
  gt_shash_delete(gtf->types, true);
  gt_shash_delete(gtf->gene_ids, true);
  gt_shash_delete(gtf->gene_types, true);
  free(gtf);
}

GT_INLINE gt_gtf_hits* gt_gtf_hits_new(void){
  gt_gtf_hits* hits = malloc(sizeof(gt_gtf_hits));
  hits->ids = gt_vector_new(16, sizeof(gt_string*));
  hits->types = gt_vector_new(16, sizeof(gt_string*));
  hits->scores = gt_vector_new(16, sizeof(float));
  hits->exonic = gt_vector_new(16, sizeof(bool));
  return hits;
}
GT_INLINE void gt_gtf_hits_delete(gt_gtf_hits* const hits){
  gt_vector_delete(hits->ids);
  gt_vector_delete(hits->scores);
  gt_vector_delete(hits->types);
  gt_vector_delete(hits->exonic);
  free(hits);
}
GT_INLINE void gt_gtf_hits_clear(gt_gtf_hits* const hits){
  gt_vector_clear(hits->ids);
  gt_vector_clear(hits->scores);
  gt_vector_clear(hits->types);
  gt_vector_clear(hits->exonic);
}


GT_INLINE gt_string* gt_gtf_get_type(const gt_gtf* const gtf, char* const type){
  if(!gt_gtf_contains_type(gtf, type)){
    gt_string* s = gt_string_new(strlen(type) + 1);
    gt_string_set_string(s, type);
    gt_shash_insert_string(gtf->types, type, s);
  }
  return gt_shash_get(gtf->types, type, gt_string);
}
GT_INLINE bool gt_gtf_contains_type(const gt_gtf* const gtf, char* const name){
	return gt_shash_is_contained(gtf->types, name);
}

GT_INLINE gt_gtf_ref* gt_gtf_get_ref(const gt_gtf* const gtf, char* const name){
  if(!gt_gtf_contains_ref(gtf, name)){
    gt_gtf_ref* rr = gt_gtf_ref_new();
    gt_shash_insert(gtf->refs, name, rr, gt_gtf_ref*);
  }
  return gt_shash_get(gtf->refs, name, gt_gtf_ref);
}
GT_INLINE bool gt_gtf_contains_ref(const gt_gtf* const gtf, char* const name){
	return gt_shash_is_contained(gtf->refs, name);
}

GT_INLINE gt_string* gt_gtf_get_gene_id(const gt_gtf* const gtf, char* const name){
  if(!gt_gtf_contains_gene_id(gtf, name)){
    const uint64_t len = strlen(name);
    gt_string* const gene_id = gt_string_new(len + 1);
    gt_string_set_nstring(gene_id, name, len);
    gt_shash_insert(gtf->gene_ids, name, gene_id, gt_string*);
  }
  return gt_shash_get(gtf->gene_ids, name, gt_string);
}
GT_INLINE bool gt_gtf_contains_gene_id(const gt_gtf* const gtf, char* const name){
	return gt_shash_is_contained(gtf->gene_ids, name);
}

GT_INLINE gt_string* gt_gtf_get_gene_type(const gt_gtf* const gtf, char* const name){
  if(!gt_gtf_contains_gene_type(gtf, name)){
    const uint64_t len = strlen(name);
    gt_string* const gene_type = gt_string_new(len + 1);
    gt_string_set_nstring(gene_type, name, len);
    gt_shash_insert(gtf->gene_types, name, gene_type, gt_string*);
  }
  return gt_shash_get(gtf->gene_types, name, gt_string);
}
GT_INLINE bool gt_gtf_contains_gene_type(const gt_gtf* const gtf, char* const name){
	return gt_shash_is_contained(gtf->gene_types, name);
}

GT_INLINE gt_gtf_node* gt_gtf_create_node(gt_vector* entries){
  const uint64_t len = gt_vector_get_used(entries);
  if(len == 0){
    return NULL;
  }
  gt_gtf_node* const node = malloc(sizeof(gt_gtf_node));
  const gt_gtf_entry* mid = *gt_vector_get_elm(entries, len/2, gt_gtf_entry*);
  node->midpoint = mid->start + ((mid->end - mid->start)/2);
  node->entries_by_end = gt_vector_new(16, sizeof(gt_gtf_entry*));
  node->entries_by_start = gt_vector_new(16, sizeof(gt_gtf_entry*));
  gt_vector* to_left = gt_vector_new(16, sizeof(gt_gtf_entry*));
  gt_vector* to_right = gt_vector_new(16, sizeof(gt_gtf_entry*));
  GT_VECTOR_ITERATE(entries, element, counter, gt_gtf_entry*){
    if((*element)->end < node->midpoint){
      gt_vector_insert(to_left, (*element), gt_gtf_entry*);
    }else if((*element)->start > node->midpoint){
      gt_vector_insert(to_right, (*element), gt_gtf_entry*);
    }else{
      gt_vector_insert(node->entries_by_end, (*element), gt_gtf_entry*);
      gt_vector_insert(node->entries_by_start, (*element), gt_gtf_entry*);
    }
  }
  // sort the start and end lists
  gt_gtf_sort_by_start(node->entries_by_start);
  gt_gtf_sort_by_end(node->entries_by_end);

  // delete incoming entry list
  gt_vector_delete(entries);
  if(gt_vector_get_used(to_left) > 0){
    // create left node
    node->left = gt_gtf_create_node(to_left);
  }else{
    node->left = NULL;
    gt_vector_delete(to_left);
  }
  if(gt_vector_get_used(to_right) > 0){
    // create right node
    node->right = gt_gtf_create_node(to_right);
  }else{
    node->right = NULL;
    gt_vector_delete(to_right);
  }
  return node;
}

/**
 * Parse a single GTF line
 */
GT_INLINE void gt_gtf_read_line(char* line, gt_gtf* const gtf){
  // skip comments
  if(line[0] == '#'){
    return;
  }
  char* ref;
  char* type;
  char* gene_id;
  char* gene_type;
  uint64_t start = 0;
  uint64_t end = 0;
  gt_strand strand = UNKNOWN;

  char * pch;
  // name
  pch = strtok(line, "\t");
  if(pch == NULL){
    return;
  }
  ref = pch;
  // source
  pch = strtok(NULL, "\t");
  if(pch == NULL){
    return;
  }
  // type
  pch = strtok(NULL, "\t");
  if(pch == NULL){
    return;
  }
  type = pch;
  // start
  pch = strtok(NULL, "\t");
  if(pch == NULL){
    return;
  }
  start = atol(pch);
  // end
  pch = strtok(NULL, "\t");
  if(pch == NULL){
    return;
  }
  end = atol(pch);
  // score
  pch = strtok(NULL, "\t");
  if(pch == NULL){
    return;
  }
  // strand
  pch = strtok(NULL, "\t");
  if(pch == NULL){
    return;
  }
  if(pch[0] == '+'){
    strand = FORWARD;
  }else if(pch[0] == '-'){
    strand = REVERSE;
  }
  // last thing where i can not remember what it was
  pch = strtok(NULL, "\t");
  if(pch == NULL){
    return;
  }
  // additional fields
  // search for gene_id
  register bool gid = false;
  register bool gene_t = false;
  while((pch = strtok(NULL, " ")) != NULL){
    if(strcmp("gene_id", pch) == 0){
      gid = true;
    }else if(strcmp("gene_type", pch) == 0){
      gene_t = true;
    }else{
      if(gid){
        gid = false;
        gene_id = pch;
        register uint64_t l = strlen(gene_id);
        register uint64_t off = 1;
        if(gene_id[l-off] == ';'){
          gene_id[l-off] = '\0';
          off = 2;
        }
        if(gene_id[0] == '"'){
          gene_id++;
          gene_id[l-(off+1)] = '\0';
        }
      }else if(gene_t){
        gene_t = false;
        gene_type = pch;
        register uint64_t l = strlen(gene_type);
        register uint64_t off = 1;
        if(gene_type[l-off] == ';'){
          gene_type[l-off] = '\0';
          off = 2;
        }
        if(gene_type[0] == '"'){
          gene_type++;
          gene_type[l-(off+1)] = '\0';
        }
        break;
      }
    }
  }
  // get the type or create it
  gt_string* tp = gt_gtf_get_type(gtf, type);
  gt_gtf_entry* e = gt_gtf_entry_new(start, end, strand, tp);
  if(gene_id != NULL){
    // get the gene_id or create it
    gt_string* gids= gt_gtf_get_gene_id(gtf, gene_id);
    e->gene_id = gids;
  }
  if(gene_type != NULL){
    // get the gene_id or create it
    gt_string* gt= gt_gtf_get_gene_type(gtf, gene_type);
    e->gene_type = gt;
  }
  // get the ref or create it
  gt_gtf_ref* gtref = gt_gtf_get_ref(gtf, ref);
  gt_vector_insert(gtref->entries, e, gt_gtf_entry*);
}

/**
 * Comparator that compares two gtf_entries by starting position
 */
GT_INLINE int gt_gtf_sort_by_start_cmp_(const gt_gtf_entry** a, const gt_gtf_entry** b){
  uint64_t p1 = (*a)->start;
  uint64_t p2 = (*b)->start;
  return p1 < p2 ? -1 : (p1>p2 ? 1 : gt_string_cmp( (*a)->type, (*b)->type ));
}
/**
 * Comparator that compares two gtf_entries by ending position
 */
GT_INLINE int gt_gtf_sort_by_end_cmp_(const gt_gtf_entry** a, const gt_gtf_entry** b){
  uint64_t p1 = (*a)->end;
  uint64_t p2 = (*b)->end;
  return p1 < p2 ? -1 : (p1>p2 ? 1 : gt_string_cmp( (*a)->type, (*b)->type ));
}
/**
 * Sort vector of gt_gtf_entries by starting position
 */
GT_INLINE void gt_gtf_sort_by_start(gt_vector* entries){
  qsort(gt_vector_get_mem(entries, gt_gtf_entry*),
    gt_vector_get_used(entries),
    sizeof(gt_gtf_entry**),
    (int (*)(const void *,const void *))gt_gtf_sort_by_start_cmp_);
}
/**
 * Sort vector of gt_gtf_entries by ending position
 */
GT_INLINE void gt_gtf_sort_by_end( gt_vector* entries){
  qsort(gt_vector_get_mem(entries, gt_gtf_entry*),
    gt_vector_get_used(entries),
    sizeof(gt_gtf_entry**),
    (int (*)(const void *,const void *))gt_gtf_sort_by_end_cmp_);
}


GT_INLINE gt_gtf* gt_gtf_read(FILE* input){
  gt_gtf* gtf = gt_gtf_new();
  char line[GTF_MAX_LINE_LENGTH];
  while ( fgets(line, GTF_MAX_LINE_LENGTH, input) != NULL ){
    gt_gtf_read_line(line, gtf);
  }
  // sort the refs
  GT_SHASH_BEGIN_ELEMENT_ITERATE(gtf->refs,shash_element,gt_gtf_ref) {
    // create a interval tree node for each ref
    shash_element->node = gt_gtf_create_node(shash_element->entries);
  } GT_SHASH_END_ITERATE
  return gtf;
}

/*
 * Binary search for start position
 */
GT_INLINE uint64_t gt_gtf_bin_search(gt_vector* const entries, const uint64_t t, const uint64_t end){
  uint64_t used = gt_vector_get_used(entries);
  uint64_t l = 0;
  uint64_t h = used - 1;
  uint64_t m = 0;

  register gt_gtf_entry* e =  *gt_vector_get_elm(entries, h, gt_gtf_entry*);
  while(l < h ){
    m = (l + h) / 2;
    e = *gt_vector_get_elm(entries, m, gt_gtf_entry*);
    if(e->start < t){
      l = m + 1;
    }else{
      h = m;
    }
  }
  e = *gt_vector_get_elm(entries, l, gt_gtf_entry*);

  if (h == l){
    return l;
  }else{
    return m;
  }
}

GT_INLINE void gt_gtf_search_node_(gt_gtf_node* node, const uint64_t start, const uint64_t end, gt_vector* const target){
  if(node == NULL) return;

  // add overlapping intervals from this node
  GT_VECTOR_ITERATE(node->entries_by_start, element, counter, gt_gtf_entry*){
    if((*element)->start > end){
      break;
    }
    if((*element)->start <= start && (*element)->end >= end){
      gt_vector_insert(target, (*element), gt_gtf_entry*);
    }
  }


  if(end < node->midpoint || start < node->midpoint){
    // search left tree
    gt_gtf_search_node_(node->left, start, end, target);
  }
  if (start > node->midpoint || end > node->midpoint){
    gt_gtf_search_node_(node->right, start, end, target);
  }

}




GT_INLINE void gt_gtf_search(const gt_gtf* const gtf,  gt_vector* const target, char* const ref, const uint64_t start, const uint64_t end){
  gt_vector_clear(target);
  // make sure the target ref is contained
  if (! gt_shash_is_contained(gtf->refs, ref)){
    return;
  }
  const gt_gtf_ref* const source_ref = gt_gtf_get_ref(gtf, ref);
  gt_gtf_search_node_(source_ref->node, start, end, target);


//  GT_VECTOR_ITERATE(target, element, counter, gt_gtf_entry*){
//    printf("\t(%s %s %d %d) (%d-%d)\n",gt_string_get_string((*element)->type),
//        gt_string_get_string((*element)->gene_id), (*element)->start, (*element)->end, start, end);
//  }
}


GT_INLINE void gt_gtf_search_template_for_exons(const gt_gtf* const gtf, gt_gtf_hits* const hits, gt_template* const template_src){
  if(!gt_gtf_contains_type(gtf, "exon")){
    return;
  }

  // paired end alignments
  gt_string* const exon_type = gt_gtf_get_type(gtf, "exon");
  gt_string* const gene_type = gt_gtf_get_type(gtf, "gene");
  // stores hits
  gt_vector* const search_hits = gt_vector_new(32, sizeof(gt_gtf_entry*));
  // clear the hits
  gt_gtf_hits_clear(hits);

  // process single alignments
  GT_TEMPLATE_IF_REDUCES_TO_ALINGMENT(template_src,alignment_src) {
    GT_ALIGNMENT_ITERATE(alignment_src,map) {
      // iterate the map blocks
      uint64_t num_map_blocks = 0;
      bool multiple_genes = false;
      gt_string* alignment_gene_id = NULL;
      gt_string* alignment_gene_type = NULL;
      bool exon_found = false;
      float overlap = 0;
      GT_MAP_ITERATE(map, map_it) {
        gt_vector_clear(search_hits);
        num_map_blocks++;

        uint64_t start = gt_map_get_begin_mapping_position(map_it);
        uint64_t end   = gt_map_get_end_mapping_position(map_it);
        char* const ref = gt_map_get_seq_name(map_it);
        // search for this block
        gt_gtf_search(gtf, search_hits, ref, start, end);
        float local_overlap = 0;

        // get all the exons with same gene id
        GT_VECTOR_ITERATE(search_hits, element, counter, gt_gtf_entry*){
          const gt_gtf_entry* const entry = *element;
          // check that the enry has a gene_id and is an exon
          if(entry->gene_id != NULL && gt_string_equals(exon_type, entry->type)){
            exon_found = true;
            if(alignment_gene_id == NULL || alignment_gene_id == entry->gene_id){
              // calculate the overlap
              float read_length = end - start;
              uint64_t tran_length = entry->end - entry->start;
              uint64_t s = entry->start < start ? start - entry->start : 0;
              uint64_t e = entry->end > end ? entry->end - end : 0;
              float over = ((tran_length - s - e) / read_length);
              if(over > local_overlap){
                local_overlap = over;
              }
              alignment_gene_id = entry->gene_id;
              alignment_gene_type = entry->gene_type;
            }else{
              if(alignment_gene_id != NULL){
                multiple_genes = true;
              }
            }
          }
        }
        if(!exon_found){
          // search for gene overlaps to mark intronic regions
          GT_VECTOR_ITERATE(search_hits, element, counter, gt_gtf_entry*){
            const gt_gtf_entry* const entry = *element;
            // check that the enry has a gene_id and is an exon
            if(entry->gene_id != NULL && gt_string_equals(gene_type, entry->type)){
              if(alignment_gene_id == NULL || alignment_gene_id == entry->gene_id){
                // calculate the overlap
                float read_length = end - start;
                uint64_t tran_length = entry->end - entry->start;
                uint64_t s = entry->start < start ? start - entry->start : 0;
                uint64_t e = entry->end > end ? entry->end - end : 0;
                float over = ((tran_length - s - e) / read_length);
                if(over > local_overlap){
                  local_overlap = over;
                }
                alignment_gene_id = entry->gene_id;
                alignment_gene_type = entry->gene_type;
              }else{
                if(alignment_gene_id != NULL){
                  multiple_genes = true;
                }
              }
            }
          }
        }
        overlap += local_overlap;
      }

      // add a hit if we found a good gene_id
      if(alignment_gene_id != NULL && !multiple_genes){
        gt_vector_insert(hits->ids, alignment_gene_id, gt_string*);
        gt_vector_insert(hits->types, alignment_gene_type, gt_string*);
        gt_vector_insert(hits->scores, (overlap / num_map_blocks), float);
        gt_vector_insert(hits->exonic, exon_found, bool);
      }else{
        gt_vector_insert(hits->ids, NULL, gt_string*);
        gt_vector_insert(hits->types, NULL, gt_string*);
        gt_vector_insert(hits->scores, 0, float);
        gt_vector_insert(hits->exonic, false, bool);
      }
    }
  } GT_TEMPLATE_END_REDUCTION__RETURN;


  GT_TEMPLATE_ITERATE_MMAP__ATTR(template_src,mmap,mmap_attr) {
    gt_string* alignment_gene_id = NULL;
    gt_string* alignment_gene_type = NULL;
    float overlap = 0;
    uint64_t num_map_blocks = 0;
    bool multiple_genes = false;
    bool exon_found = false;
    GT_MMAP_ITERATE(mmap, map, end_position){
      GT_MAP_ITERATE(map, map_it){
        gt_vector_clear(search_hits);
        num_map_blocks++;

        uint64_t start = gt_map_get_begin_mapping_position(map_it);
        uint64_t end   = gt_map_get_end_mapping_position(map_it);
        char* const ref = gt_map_get_seq_name(map_it);
        // search for this block
        gt_gtf_search(gtf, search_hits, ref, start, end);
        float local_overlap = 0;
        // get all the exons with same gene id
        GT_VECTOR_ITERATE(search_hits, element, counter, gt_gtf_entry*){
          const gt_gtf_entry* const entry = *element;
//          printf("\tSearch hit : %s %d-%d\n", gt_string_get_string(entry->type), entry->start, entry->end);
          // check that the enry has a gene_id and is an exon
          if(entry->gene_id != NULL && gt_string_equals(exon_type, entry->type)){
            exon_found = true;
            if(alignment_gene_id == NULL || alignment_gene_id == entry->gene_id){
              // calculate the overlap
              float read_length = end - start;
              uint64_t tran_length = entry->end - entry->start;
              uint64_t s = entry->start < start ? start - entry->start : 0;
              uint64_t e = entry->end > end ? entry->end - end : 0;
              float over = ((tran_length - s - e) / read_length);
              if(over > local_overlap){
                local_overlap = over;
              }
              alignment_gene_id = entry->gene_id;
              alignment_gene_type = entry->gene_type;
            }else{
              if(alignment_gene_id != NULL){
                multiple_genes = true;
              }
            }
          }
        }
        if(!exon_found && gt_vector_get_used(search_hits) > 0){
//          printf("No exon found ... checking for gene annotation %d %d-%d\n", gt_vector_get_used(search_hits), start, end);
          GT_VECTOR_ITERATE(search_hits, element, counter, gt_gtf_entry*){
            const gt_gtf_entry* const entry = *element;
//            printf("TYPE: %s :: %s :: %d-%d\n", gt_string_get_string(entry->type), gt_string_get_string(entry->gene_id), entry->start, entry->end);
            // check that the enry has a gene_id and is an exon
            if(entry->gene_id != NULL && gt_string_equals(gene_type, entry->type)){
              if(alignment_gene_id == NULL || alignment_gene_id == entry->gene_id){
//                printf("Found gene ....\n\n");
                // calculate the overlap
                float read_length = end - start;
                uint64_t tran_length = entry->end - entry->start;
                uint64_t s = entry->start < start ? start - entry->start : 0;
                uint64_t e = entry->end > end ? entry->end - end : 0;
                float over = ((tran_length - s - e) / read_length);
                if(over > local_overlap){
                  local_overlap = over;
                }
                alignment_gene_id = entry->gene_id;
                alignment_gene_type = entry->gene_type;
              }else{
                if(alignment_gene_id != NULL){
                  multiple_genes = true;
                }
              }
            }
          }
        }
        overlap += local_overlap;
      }
    }
    // add a hit if we found a good gene_id
    if(alignment_gene_id != NULL && !multiple_genes){
      gt_vector_insert(hits->ids, alignment_gene_id, gt_string*);
      gt_vector_insert(hits->types, alignment_gene_type, gt_string*);
      gt_vector_insert(hits->scores, (overlap / num_map_blocks), float);
      gt_vector_insert(hits->exonic, exon_found, bool);
    }else{
      gt_vector_insert(hits->ids, NULL, gt_string*);
      gt_vector_insert(hits->types, NULL, gt_string*);
      gt_vector_insert(hits->scores, 0, float);
      gt_vector_insert(hits->exonic, false, bool);
    }
  }
  gt_vector_delete(search_hits);
}

