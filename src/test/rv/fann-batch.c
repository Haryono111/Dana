#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <getopt.h>

#include "fixedfann.h"
#include "xfiles.h"

#define read_csr(reg) ({ unsigned long __tmp; \
  asm volatile ("csrr %0, " #reg : "=r"(__tmp)); \
  __tmp; })

static char * usage_message =
  "fann-batch -n[config] -t[train file] -b[binary point] [options]\n"
  "Run batch training on a specific neural network and training file.\n"
  "\n"
  "Options:\n"
  "  -b, --binary-point         the binary point (number of fractional bits)\n"
  "  -c, --stat-cycles          print the total number of cycles in the ROI\n"
  "  -e, --max-epochs           the epoch limit (default 10k)\n"
  "  -f, --bit-fail-limit       sets the bit fail limit (default 0.05)\n"
  "  -g, --mse-fail-limit       sets the maximum MSE (default -1, i.e., off)\n"
  "  -h, --help                 print this help and exit\n"
  "  -i, --id                   numeric id to use for printing data (default 0)\n"
  "  -l, --stat-last            print last epoch number statistic\n"
  "  -m, --stat-mse             print mse statistics\n"
  "  -n, --nn-config            the binary NN configuration to use\n"
  "  -p, --performance-mode     runs until an epoch limit, all checks disabled\n"
  "  -t, --train-file           the fixed point FANN training file to use\n"
  "  -v, --verbose              turn on per-item inputs/output printfs\n"
  "\n"
  "Flags -n, -t, and -b are required.\n"
  "When gathering data related to connection updates per second, -p\n"
  "should always be used as this diables all unnecessary control statements.\n";

void usage() {
  printf("Usage: %s", usage_message);
}

// Read the binary configuration format and return the number of
// connections
uint64_t binary_config_num_connections(char * file_nn) {
  FILE * fp;
  int i;

  fp = fopen(file_nn, "rb");
  if (fp == NULL) {
    fprintf(stderr, "[ERROR] Unable to opn %s\n\n", file_nn);
    return 0;
  }

  uint64_t connections = 0;
  uint16_t total_layers, layer_offset, ptr;
  uint32_t tmp;
  uint16_t layer_0, layer_1;
  fseek(fp, 6, SEEK_SET);
  fread(&total_layers, sizeof(uint16_t), 1, fp);
  fread(&layer_offset, sizeof(uint16_t), 1, fp);
  fseek(fp, layer_offset, SEEK_SET);
  fread(&ptr, sizeof(uint16_t), 1, fp);
  ptr &= ~((~0)<<12);
  fseek(fp, ptr + 2, SEEK_SET);
  fread(&layer_0, sizeof(uint16_t), 1, fp);
  layer_0 &= ~((~0)<<8);
  fseek(fp, layer_offset, SEEK_SET);
  fread(&tmp, sizeof(uint32_t), 1, fp);
  layer_1 = (tmp & (~((~0)<<10))<<12)>>12;
  connections += (layer_0 + 1) * layer_1;

  for (i = 1; i < total_layers; i++) {
    layer_0 = layer_1;
    fseek(fp, layer_offset + 4 * i, SEEK_SET);
    fread(&tmp, sizeof(uint32_t), 1, fp);
    layer_1 = (tmp & (~((~0)<<10))<<12)>>12;
    connections += (layer_0 + 1) * layer_1;
  }

  fclose(fp);
  return connections;
}

int main (int argc, char * argv[]) {
  int exit_code = 0, max_epochs = 10000, bits_failing = -1, id = 0;
  int flag_cycles = 0, flag_last = 0, flag_mse = 0, flag_performance = 0,
    flag_verbose = 0;
  uint64_t cycles;
  float bit_fail_limit = 0.05, mse_fail_limit = -1.0;
  struct fann_train_data * data = NULL;

  char * file_nn = NULL, * file_train = NULL;
  asid_nnid_table * table = NULL;
  element_type * outputs = NULL;
  int binary_point = -1, c;
  while (1) {
    static struct option long_options[] = {
      {"binary-point",     required_argument, 0, 'b'},
      {"stat-cycles",      no_argument,       0, 'c'},
      {"max-epochs",       required_argument, 0, 'e'},
      {"bit-fail-limit",   required_argument, 0, 'f'},
      {"mse-fail-limit",   required_argument, 0, 'g'},
      {"help",             no_argument,       0, 'h'},
      {"id",               required_argument, 0, 'i'},
      {"stat-last",        no_argument,       0, 'l'},
      {"stat-mse",         no_argument,       0, 'm'},
      {"nn-config",        required_argument, 0, 'n'},
      {"performance-mode", no_argument,       0, 'p'},
      {"train-file",       required_argument, 0, 't'},
      {"verbose",          no_argument,       0, 'v'}
    };
    int option_index = 0;
    c = getopt_long (argc, argv, "b:ce:f:g:hi:lmn:pt:v", long_options, &option_index);
    if (c == -1)
      break;
    switch (c) {
    case 'b':
      binary_point = atoi(optarg);
      break;
    case 'c':
      flag_cycles = 1;
      break;
    case 'e':
      max_epochs = atoi(optarg);
      break;
    case 'f':
      bit_fail_limit = atof(optarg);
      break;
    case 'g':
      mse_fail_limit = atof(optarg);
      break;
    case 'h':
      usage();
      exit_code = 0;
      goto bail;
      break;
    case 'i':
      id = atoi(optarg);
      break;
    case 'l':
      flag_last = 1;
      break;
    case 'm':
      flag_mse = 1;
      break;
    case 'n':
      file_nn = optarg;
      break;
    case 'p':
      flag_performance = 1;
      break;
    case 't':
      file_train = optarg;
      break;
    case 'v':
      flag_verbose = 1;
      break;
    }
  };

  // Make sure there aren't any arguments left over
  if (optind != argc) {
    fprintf(stderr, "[ERROR] Bad argument\n\n");
    usage();
    exit_code = -1;
    goto bail;
  }

  // Make sure we have all required inputs
  if (file_nn == NULL || file_train == NULL || binary_point == -1) {
    fprintf(stderr, "[ERROR] Missing required input argument\n\n");
    usage();
    exit_code = -1;
    goto bail;
  }

  // Create the ASID--NNID Table
  asid_nnid_table_create(&table, 4, 17);
  set_antp(table);

  // Populate the ASID--NNID Table
  asid_type asid = 1;
  nnid_type nnid = 0;
  attach_nn_configuration(&table, asid, file_nn);
  set_asid(asid);

  uint64_t connections_per_epoch = binary_config_num_connections(file_nn);

  // Read in data from the training file
  data = fann_read_train_from_file(file_train);
  if (data == NULL) {
    exit_code = -2;
    goto bail;
  }

  float multiplier = pow(2, binary_point);

  // Train on the provided data
  int epoch, item, i;
  tid_type tid;
  float mse, error;

  outputs = (element_type *) malloc(data->num_output * sizeof(element_type));
  int batch_items = fann_length_train_data(data);
  int32_t learn_rate = (int32_t)((0.7 / batch_items) * multiplier);

  // Execution is broken down into two different modes "performance"
  // and "verbose". "Verbose" allows for early training exits based on
  // MSE, etc. However, this requires additional control statements,
  // possible printfs and things which hide the actual performance of
  // the accelerator. "Performance" turns all this off and just runs
  // for a hard number of epochs. This mode is intended to be used
  // with the '-c' option to get an accurate number of connection
  // updates per cycle.
  if (flag_performance) goto xfiles_performance;
  else goto xfiles_verbose;

 xfiles_verbose:
  cycles = read_csr(0xc00);
  for (epoch = 0; epoch < max_epochs; epoch++) {
    // Run one training epoch
    tid = new_write_request(nnid, 2, 0);
    write_register(tid, xfiles_reg_batch_items, batch_items);
    write_register(tid, xfiles_reg_learning_rate, learn_rate);
    write_register(tid, xfiles_reg_weight_decay_lambda, 0);

    for (item = 0; item < batch_items; item++) {
      // Write the output and input data
      write_data_train_incremental(tid, (element_type *) data->input[item],
                                   (element_type *) data->output[item],
                                   data->num_input, data->num_output);

      // Blocking read
      read_data_spinlock(tid, outputs, data->num_output);
    }

    // Check the outputs
    bits_failing = 0;
    if (flag_mse)
      mse = 0.0;
    for (item = 0; item < batch_items; item++) {
      tid = new_write_request(nnid, 0, 0);
      write_data(tid, (element_type *) data->input[item], data->num_input);
      read_data_spinlock(tid, outputs, data->num_output);

      if (flag_verbose) {
        printf("[INFO] ");
        for (i = 0; i < data->num_input; i++) {
          printf("%8.5f ", ((float)data->input[item][i]) / multiplier);
        }
      }

      for (i = 0; i < data->num_output; i++) {
        if (flag_verbose)
          printf("%8.5f ", ((float)outputs[i])/multiplier);
        bits_failing +=
          fabs((float)(outputs[i] - data->output[item][i]) / multiplier) >
          bit_fail_limit;
        if (flag_mse) {
          error = (float)(outputs[i] - data->output[item][i]) / multiplier;
          mse += error * error;
        }
      }

      if (flag_verbose) {
        if (item < batch_items - 1)
          printf("\n");
      }
    }


    if (flag_verbose)
      printf("%5d\n\n", epoch);
    if (flag_mse) {
      mse /= batch_items;
      printf("[STAT] epoch %d id %d bp %d mse %8.8f\n", epoch, id, binary_point, mse);
    }
    if (bits_failing == 0 || mse < mse_fail_limit)
      goto finish;
  }
  goto finish;

 xfiles_performance:
  cycles = read_csr(0xc00);
  for (epoch = 0; epoch < max_epochs; epoch++) {
    // Run one training epoch
    tid = new_write_request(nnid, 2, 0);
    write_register(tid, xfiles_reg_batch_items, batch_items);
    write_register(tid, xfiles_reg_learning_rate, learn_rate);
    write_register(tid, xfiles_reg_weight_decay_lambda, 0);

    for (item = 0; item < batch_items; item++) {
      // Write the output and input data
      write_data_train_incremental(tid, (element_type *) data->input[item],
                                   (element_type *) data->output[item],
                                   data->num_input, data->num_output);

      // Blocking read
      read_data_spinlock(tid, outputs, data->num_output);
    }
  }
  goto finish;

  // Print overall statistics in a parser-friendly way
 finish:
  cycles = read_csr(0xc00) - cycles;
  // printf("# [STAT] fann-batch-id%d-bit-fail %d\n", id, bits_failing);
  // printf("# [STAT] fann-batch-id%d-final-epoch %d\n", id, epoch);
  if (flag_last)
    printf("[STAT] bp %d id %d epoch %d\n", binary_point, id, epoch);
  if (flag_cycles) {
    printf("[STAT] bp %d id %d cycles %ld\n", binary_point, id, cycles);
    printf("[STAT] bp %d id %d CUPC %0.8f\n", binary_point, id,
           (connections_per_epoch * epoch) / (double) cycles);
  }

  // Free memory
 bail:
  if (data != NULL)
    fann_destroy_train(data);
  if (table != NULL)
    asid_nnid_table_destroy(&table);
  if (outputs != NULL)
    free(outputs);
  return exit_code;
}
