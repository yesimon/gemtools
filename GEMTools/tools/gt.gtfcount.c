/*
 * PROJECT: GEM-Tools library
 * FILE: gt.gtfcount.c
 * DATE: 10/07/2013
 * AUTHOR(S): Thasso Griebel <thasso.griebel@gmail.com>
 * DESCRIPTION: Annotation map a file against a reference annotation
 */

#include <getopt.h>
#include <omp.h>

#include "gem_tools.h"

typedef struct {
  char *input_file;
  char *output_file;
  char *gene_counts_file;
  char *annotation;
  bool shell;
  bool paired;
  bool verbose;
  uint64_t num_threads;
} gt_gtfcount_args;

typedef struct {
  uint64_t single_genes;
  uint64_t multi_genes;
  uint64_t no_genes;
  uint64_t single_reads;
} gt_gtfcount_pair_counts;


gt_gtfcount_args parameters = {
    .input_file=NULL,
    .output_file=NULL,
    .gene_counts_file=NULL,
    .annotation=NULL,
    .paired=false,
    .shell=false,
    .verbose=false,
    .num_threads=1
};



GT_INLINE void gt_gtfcount_merge_counts_(gt_shash* const source, gt_shash* const target){
  GT_SHASH_BEGIN_ITERATE(source, key, value, uint64_t){
    if(!gt_shash_is_contained(target, key)){
      uint64_t* v = gt_malloc_uint64();
      *v = *value;
      gt_shash_insert(target, key, v, uint64_t);
    }else{
      uint64_t* v = gt_shash_get(target,key,uint64_t);
      *v += (*value);
    }

  }GT_SHASH_END_ITERATE;
}



uint64_t gt_gtfcount_read(gt_gtf* const gtf, gt_shash* const gene_counts, gt_shash* const type_counts, gt_shash* const gene_type_counts, gt_shash* const pair_patterns_counts, gt_gtfcount_pair_counts* pair_counts) {
  // Open file IN/OUT
  gt_input_file* input_file = NULL;
  uint64_t i = 0;
  if(parameters.input_file != NULL){
    input_file = gt_input_file_open(parameters.input_file,false);
  }else{
    input_file = gt_input_stream_open(stdin);
  }
  // create maps for the threads
  gt_shash** gene_counts_list = gt_calloc(parameters.num_threads, gt_shash*, true);
  gt_shash** type_counts_list = gt_calloc(parameters.num_threads, gt_shash*, true);
  gt_shash** gene_type_counts_list = gt_calloc(parameters.num_threads, gt_shash*, true);
  gt_shash** pair_patterns_list = gt_calloc(parameters.num_threads, gt_shash*, true);
  uint64_t* singel_gene_pairs = malloc(parameters.num_threads * sizeof(uint64_t));
  uint64_t* multi_gene_pairs = malloc(parameters.num_threads * sizeof(uint64_t));
  uint64_t* no_gene_pairs = malloc(parameters.num_threads * sizeof(uint64_t));
  uint64_t* single_ends = malloc(parameters.num_threads * sizeof(uint64_t));

  for(i=0; i<parameters.num_threads; i++){
    gene_counts_list[i] = gt_shash_new();
    type_counts_list[i] = gt_shash_new();
    gene_type_counts_list[i] = gt_shash_new();
    pair_patterns_list[i] = gt_shash_new();
    singel_gene_pairs[i] = 0;
    multi_gene_pairs[i] = 0;
    no_gene_pairs[i] = 0;
    single_ends[i] = 0;
  }
  uint64_t* read_counts = malloc(parameters.num_threads*sizeof(uint64_t));
  for(i=0; i<parameters.num_threads; i++){
    read_counts[i] = 0;
  }
  // Parallel reading+process
  #pragma omp parallel num_threads(parameters.num_threads)
  {
    gt_buffered_input_file* buffered_input = gt_buffered_input_file_new(input_file);
    gt_status error_code;
    gt_template* template = gt_template_new();
    gt_generic_parser_attributes* generic_parser_attr = gt_input_generic_parser_attributes_new(parameters.paired);

    // local maps
    uint64_t tid = omp_get_thread_num();
    gt_shash* l_gene_counts = gene_counts_list[tid];
    gt_shash* l_type_counts = type_counts_list[tid];
    gt_shash* l_gene_type_counts = gene_type_counts_list[tid];
    gt_shash* l_pair_patterns = pair_patterns_list[tid];

    while ((error_code = gt_input_generic_parser_get_template(buffered_input,template,generic_parser_attr))) {
      if (error_code != GT_IMP_OK) {
        gt_fatal_error_msg("Fatal error parsing file \n");
      }

      if (gt_template_get_num_blocks(template)==1){
        GT_TEMPLATE_REDUCTION(template,alignment);
        if(gt_alignment_is_mapped(alignment) && gt_alignment_get_num_maps(alignment) == 1){
          single_ends[tid] += 1;
          read_counts[tid]++;
        }
        gt_gtf_count_alignment(gtf, alignment, l_type_counts, l_gene_counts, l_gene_type_counts);

      } else {
        if (!gt_template_is_mapped(template)) {
          GT_TEMPLATE_REDUCE_BOTH_ENDS(template,alignment_end1,alignment_end2);

          if(gt_alignment_is_mapped(alignment_end1)&& gt_alignment_get_num_maps(alignment_end1) == 1){
            read_counts[tid]++;
            single_ends[tid] += 1;
          }
          gt_gtf_count_alignment(gtf, alignment_end1, l_type_counts, l_gene_counts, l_gene_type_counts);


          if(gt_alignment_is_mapped(alignment_end2)&& gt_alignment_get_num_maps(alignment_end2) == 1){
            read_counts[tid]++;
            single_ends[tid] += 1;
          }
          gt_gtf_count_alignment(gtf, alignment_end2, l_type_counts, l_gene_counts, l_gene_type_counts);

        } else {
          uint64_t hits_genes = gt_gtf_count_template(gtf, template, l_type_counts, l_gene_counts, l_gene_type_counts, l_pair_patterns);
          if(gt_template_is_mapped(template) && gt_template_get_num_mmaps(template) == 1){
            read_counts[tid] += 2;
            if(hits_genes == 1){
              singel_gene_pairs[tid]++;
            }else if(hits_genes > 1){
              multi_gene_pairs[tid]++;
            }else{
              no_gene_pairs[tid]++;
            }
          }
        }
      }
    }
    // Clean
    gt_template_delete(template);
    gt_buffered_input_file_close(buffered_input);
  }
  uint64_t total_counts =0;
  // merge the count tables and delete them
  for(i=0; i<parameters.num_threads; i++){
    gt_gtfcount_merge_counts_(gene_counts_list[i], gene_counts);
    gt_gtfcount_merge_counts_(type_counts_list[i], type_counts);
    gt_gtfcount_merge_counts_(gene_type_counts_list[i], gene_type_counts);
    gt_gtfcount_merge_counts_(pair_patterns_list[i], pair_patterns_counts);
    gt_shash_delete(gene_counts_list[i], true);
    gt_shash_delete(type_counts_list[i], true);
    gt_shash_delete(gene_type_counts_list[i], true);
    total_counts += read_counts[i];

    pair_counts->multi_genes += multi_gene_pairs[i];
    pair_counts->single_genes += singel_gene_pairs[i];
    pair_counts->no_genes += no_gene_pairs[i];
    pair_counts->single_reads += single_ends[i];
  }
  // Clean
  gt_input_file_close(input_file);
  return total_counts;
}


void parse_arguments(int argc,char** argv) {
  struct option* gt_gtfcount_getopt = gt_options_adaptor_getopt(gt_gtfcount_options);
  gt_string* const gt_gtfcount_short_getopt = gt_options_adaptor_getopt_short(gt_gtfcount_options);

  int option, option_index;
  while (true) {
    // Get option & Select case
    if ((option=getopt_long(argc,argv,
        gt_string_get_string(gt_gtfcount_short_getopt),gt_gtfcount_getopt,&option_index))==-1) break;
    switch (option) {
    /* I/O */
    case 'i':
      parameters.input_file = optarg;
      break;
    case 'o':
      parameters.output_file = optarg;
      break;
    case 'g':
      parameters.gene_counts_file = optarg;
      break;
    case 'a':
      parameters.annotation = optarg;
      break;
    case 'p':
      parameters.paired = true;
      break;
    /* Misc */
    case 500:
      parameters.shell = true;
      break;
    case 'v':
      parameters.verbose = true;
      break;
    case 't':
      parameters.num_threads = atol(optarg);
      break;
    case 'h':
      fprintf(stderr, "USE: gt.gtfcount [OPERATION] [ARGS]...\n");
      gt_options_fprint_menu(stderr,gt_gtfcount_options,gt_gtfcount_groups,false,false);
      exit(1);
    case 'J':
      gt_options_fprint_json_menu(stderr,gt_gtfcount_options,gt_gtfcount_groups,true,false);
      exit(1);
      break;
    case '?':
    default:
      gt_fatal_error_msg("Option not recognized");
    }
  }
  // Check parameters
  if (parameters.annotation==NULL) {
    gt_fatal_error_msg("Please specify a reference annotation");
  }
  // Free
  gt_string_delete(gt_gtfcount_short_getopt);
}

GT_INLINE void gt_gtfcount_warn(const char* const msg){
  if(parameters.verbose){
    fprintf(stderr, "%s", msg);
  }
}

GT_INLINE uint64_t gt_gtfcount_get_count_(gt_shash* const table, char* const element){
  if(!gt_shash_is_contained(table, element)){
    return 0;
  }
  uint64_t* v = gt_shash_get(table,element,uint64_t);
  return *v;
}

GT_INLINE char* gt_gtfcount_shell_parse_ref(char** line){
  char* ref = *line;
  GT_READ_UNTIL(line, **line==':');
  if(GT_IS_EOL(line))return NULL;
  **line = EOS;
  GT_NEXT_CHAR(line);
  return ref;
}

GT_INLINE uint64_t gt_gtfcount_shell_parse_start(char** line){
  char* ref = *line;
  uint64_t n = 0;
  GT_READ_UNTIL(line, **line=='-' || **line=='\n');
  **line = EOS;
  n = atol(ref);
  GT_NEXT_CHAR(line);
  return n;
}

GT_INLINE uint64_t gt_gtfcount_shell_parse_end(char** line){
  if(**line == '\n') return 0;
  char* ref = *line;
  uint64_t n = 0;
  GT_READ_UNTIL(line, **line=='\n');
  **line = EOS;
  n = atol(ref);
  GT_NEXT_CHAR(line);
  return n;
}

int main(int argc,char** argv) {
  // GT error handler
  gt_handle_error_signals();
  parse_arguments(argc,argv);

  // read gtf file
  gt_gtfcount_warn("Reading GTF...");
  gt_gtf* const gtf = gt_gtf_read_from_file(parameters.annotation, parameters.num_threads);
  gt_gtfcount_warn("Done\n");

  if(parameters.shell){
    fprintf(stdout, "Search the annotation with queries like : <ref>:<start>[-<end>]\n");
    fprintf(stdout, ">");
    gt_vector* hits = gt_vector_new(32, sizeof(gt_gtf_entry*));
    size_t buf_size = 1024;
    ssize_t read;
    char* line = malloc(buf_size * sizeof(char));
    uint64_t start, end=0 ;
    while((read = getline(&line, &buf_size, stdin)) != -1){
      // parse the line
      char* ref = gt_gtfcount_shell_parse_ref(&line);
      if(ref==NULL){
        fprintf(stdout, "Unable to parse reference name.\n");
        fprintf(stdout, ">");
        continue;
      }
      start = gt_gtfcount_shell_parse_start(&line);
      end = gt_gtfcount_shell_parse_end(&line);
      if(end != 0 && start > end){
        fprintf(stdout, "start > end not allowed!\n");
        fprintf(stdout, ">");
        continue;
      }
      if(end == 0) end = start;
      uint64_t num_results = gt_gtf_search(gtf,hits, ref, start, end, true);
      if(num_results == 0){
        fprintf(stdout, "Nothing found :(\n");
      }else{
        GT_VECTOR_ITERATE(hits, v, c, gt_gtf_entry*){
          gt_gtf_entry* e = *v;
          gt_gtf_print_entry_(e, NULL);
        }
      }
      fprintf(stdout, ">");
    }
    //free(line);
    exit(0);
  }
  // local maps
  gt_shash* gene_counts = gt_shash_new();
  gt_shash* gene_type_counts = gt_shash_new();
  gt_shash* type_counts = gt_shash_new();
  gt_shash* pair_pattern_counts = gt_shash_new();

  gt_gtfcount_pair_counts pair_counts = {
      .single_genes = 0,
      .multi_genes = 0,
      .no_genes= 0,
      .single_reads =0
  };

  gt_gtfcount_warn("Counting...");
  uint64_t total_counts = gt_gtfcount_read(gtf, gene_counts, type_counts, gene_type_counts, pair_pattern_counts, &pair_counts);
  gt_gtfcount_warn("Done\n");

  FILE* output = stdout;
  if(parameters.output_file != NULL){
    output = fopen(parameters.output_file, "w");
    if(output == NULL){
      gt_perror();
      exit(1);
    }
  }

  /*
   * Print type counts for hits to single genese and hits to multiple genes
   */
  uint64_t type_counts_single_total = 0;
  uint64_t type_counts_multi_total = 0;
  GT_SHASH_BEGIN_ITERATE(type_counts, key, e, uint64_t){
    if(strstr(key, "_mg") == NULL){
      type_counts_single_total += *e;
    }else{
      type_counts_multi_total += *e;
    }
  }GT_SHASH_END_ITERATE

  fprintf(output, "Annotation type counts for single gene hits (Single: %"PRIu64" (%.2f%%))\n", type_counts_single_total, (((float)type_counts_single_total/total_counts) * 100.0));
  fprintf(output, "-----------------------------------------------------------------------\n");
  GT_SHASH_BEGIN_ITERATE(type_counts, key, e, uint64_t){
    if(strstr(key, "_mg") == NULL){
      fprintf(output, "  %40s: %"PRIu64" (%.5f%%)\n", key, *e, (((float)*e/(float)type_counts_single_total) * 100.0));
    }
  }GT_SHASH_END_ITERATE
  fprintf(output, "-----------------------------------------------------------------------\n");
  fprintf(output, "Annotation type counts for multi gene hits (Multi: %"PRIu64" (%.2f%%))\n", type_counts_multi_total, (((float)type_counts_multi_total/total_counts) * 100.0));
  fprintf(output, "-----------------------------------------------------------------------\n");
  GT_SHASH_BEGIN_ITERATE(type_counts, key, e, uint64_t){
    if(strstr(key, "_mg") != NULL){
      fprintf(output, "  %40s: %"PRIu64" (%.5f%%)\n", key, *e, (((float)*e/(float)type_counts_multi_total) * 100.0));
    }
  }GT_SHASH_END_ITERATE
  fprintf(output, "-----------------------------------------------------------------------\n");

  /*
   * Print Gene Types
   */
  uint64_t gene_types_total = 0;
  GT_SHASH_BEGIN_ELEMENT_ITERATE(gene_type_counts, e, uint64_t){
    gene_types_total += *e;
  }GT_SHASH_END_ITERATE
  fprintf(output, "Gene type counts for single gene Exons (%"PRIu64")\n", gene_types_total);
  fprintf(output, "-----------------------------------------------------------------------\n");
  GT_SHASH_BEGIN_ITERATE(gene_type_counts, key, e, uint64_t){
    fprintf(output, "  %40s: %"PRIu64" (%.5f%%)\n", key, *e, (((float)*e/(float)gene_types_total) * 100.0));
  }GT_SHASH_END_ITERATE
  fprintf(output, "-----------------------------------------------------------------------\n");


  /*
   * Print Pair patterns
   */
  uint64_t paired_pattern_total = 0;
  GT_SHASH_BEGIN_ELEMENT_ITERATE(pair_pattern_counts, e, uint64_t){
    paired_pattern_total += *e;
  }GT_SHASH_END_ITERATE

  fprintf(output, "Paired-read patterns (%"PRIu64")\n", paired_pattern_total);
  fprintf(output, "-----------------------------------------------------------------------\n");
  GT_SHASH_BEGIN_ITERATE(pair_pattern_counts, key, e, uint64_t){
    fprintf(output, "  %40s: %"PRIu64" (%.5f%%)\n", key, *e, (((float)*e/(float)paired_pattern_total) * 100.0));
  }GT_SHASH_END_ITERATE
  fprintf(output, "-----------------------------------------------------------------------\n");

  uint64_t paired_total = pair_counts.multi_genes + pair_counts.single_genes + pair_counts.no_genes + pair_counts.single_reads;
  fprintf(output, "Paired-reads Gene-Matches (pairs: %"PRIu64" singles: %"PRIu64" total: %"PRIu64")\n", paired_total-pair_counts.single_reads, pair_counts.single_reads, paired_total);
  fprintf(output, "-----------------------------------------------------------------------\n");
  fprintf(output, "  %40s: %"PRIu64" (%.5f%%)\n", "Single end reads", pair_counts.single_reads, (((float)pair_counts.single_reads/(float)paired_total) * 100.0));
  fprintf(output, "  %40s: %"PRIu64" (%.5f%%)\n", "Pair not mapped to gene", pair_counts.no_genes, (((float)pair_counts.no_genes/(float)paired_total) * 100.0));
  fprintf(output, "  %40s: %"PRIu64" (%.5f%%)\n", "Pair mapped to single gene", pair_counts.single_genes, (((float)pair_counts.single_genes/(float)paired_total) * 100.0));
  fprintf(output, "  %40s: %"PRIu64" (%.5f%%)\n", "Pair mapped to multiple genes", pair_counts.multi_genes, (((float)pair_counts.multi_genes/(float)paired_total) * 100.0));
  fprintf(output, "-----------------------------------------------------------------------\n");





  if(parameters.output_file != NULL){
    fclose(output);
  }

  if(parameters.gene_counts_file != NULL){
    output = fopen(parameters.gene_counts_file, "w");
    if(output == NULL){
      gt_perror();
      exit(1);
    }
    // print gene table
    GT_SHASH_BEGIN_ITERATE(gene_counts, key, e, uint64_t){
      fprintf(output, "%s\t%"PRIu64"\n", key, *e );
    }GT_SHASH_END_ITERATE
    fclose(output);
  }


  gt_shash_delete(gene_counts, true);
  gt_shash_delete(type_counts, true);
  gt_shash_delete(gene_type_counts, true);
  return 0;
}


