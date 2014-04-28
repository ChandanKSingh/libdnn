#include <iostream>
#include <string>
#include <dnn.h>
#include <dnn-utility.h>
#include <cmdparser.h>
#include <rbm.h>
#include <batch.h>
using namespace std;

size_t dnn_predict(const DNN& dnn, DataSet& data, ERROR_MEASURE errorMeasure);
void dnn_train(DNN& dnn, DataSet& train, DataSet& valid, size_t batchSize, ERROR_MEASURE errorMeasure);
bool isEoutStopDecrease(const std::vector<size_t> Eout, size_t epoch, size_t nNonIncEpoch);

int main (int argc, char* argv[]) {

  CmdParser cmd(argc, argv);

  cmd.add("training_set_file")
     .add("model_in")
     .add("model_out", false);

  cmd.addGroup("Feature options:")
     .add("--input-dim", "specify the input dimension (dimension of feature).\n"
	 "0 for auto detection.")
     .add("--normalize", "Feature normalization: \n"
	"0 -- Do not normalize.\n"
	"1 -- Rescale each dimension to [0, 1] respectively.\n"
	"2 -- Normalize to standard score. z = (x-u)/sigma .", "0")
     .add("--base", "Label id starts from 0 or 1 ?", "0");

  cmd.addGroup("Training options: ")
     .add("-v", "ratio of training set to validation set (split automatically)", "5")
     .add("--max-epoch", "number of maximum epochs", "100000")
     .add("--min-acc", "Specify the minimum cross-validation accuracy", "0.5")
     .add("--learning-rate", "learning rate in back-propagation", "0.01")
     .add("--variance", "the variance of normal distribution when initializing the weights", "0.01")
     .add("--batch-size", "number of data per mini-batch", "32")
     .add("--type", "choose one of the following:\n"
	"0 -- classfication\n"
	"1 -- regression", "0");

  cmd.addGroup("Example usage: dnn-train data/train3.dat --nodes=16-8");

  if (!cmd.isOptionLegal())
    cmd.showUsageAndExit();

  string train_fn     = cmd[1];
  string model_in     = cmd[2];
  string model_out    = cmd[3];

  size_t input_dim    = cmd["--input-dim"];
  string n_type	      = cmd["--normalize"];
  int base	      = cmd["--base"];

  int ratio	      = cmd["-v"];
  size_t batchSize    = cmd["--batch-size"];
  float learningRate  = cmd["--learning-rate"];
  float variance      = cmd["--variance"];
  float minValidAcc   = cmd["--min-acc"];
  size_t maxEpoch     = cmd["--max-epoch"];

  // Set configurations
  Config config;
  config.variance = variance;
  config.learningRate = learningRate;
  config.minValidAccuracy = minValidAcc;
  config.maxEpoch = maxEpoch;

  // Load model
  DNN dnn(model_in);
  dnn.setConfig(config);

  // Load data
  DataSet data(train_fn, input_dim);
  data.normalize(n_type);
  data.checkLabelBase(base);
  data.shuffle();
  data.showSummary();

  DataSet train, valid;
  data.splitIntoTrainAndValidSet(train, valid, ratio);
  config.print();

  // Start Training
  ERROR_MEASURE err = CROSS_ENTROPY;
  dnn_train(dnn, train, valid, batchSize, err);

  // Save the model
  if (model_out.empty())
    model_out = train_fn.substr(train_fn.find_last_of('/') + 1) + ".model";

  dnn.save(model_out);

  return 0;
}

void dnn_train(DNN& dnn, DataSet& train, DataSet& valid, size_t batchSize, ERROR_MEASURE errorMeasure) {

  printf("Training...\n");
  perf::Timer timer;
  timer.start();

  vector<mat> O(dnn.getNLayer());

  size_t Ein = 1;
  size_t MAX_EPOCH = dnn.getConfig().maxEpoch, epoch;
  std::vector<size_t> Eout;
  Eout.reserve(MAX_EPOCH);

  size_t nTrain = train.size(),
	 nValid = valid.size();

  mat fout;

  for (epoch=0; epoch<MAX_EPOCH; ++epoch) {

    Batches batches(batchSize, nTrain);
    for (Batches::iterator itr = batches.begin(); itr != batches.end(); ++itr) {

      // Copy a batch of data from host to device
      mat fin = train.getX(*itr);

      dnn.feedForward(fout, fin);

      mat error = getError( train.getY(*itr), fout, errorMeasure);

      dnn.backPropagate(error, fin, fout, dnn.getConfig().learningRate);
    }

    Ein = dnn_predict(dnn, train, errorMeasure);
    Eout.push_back(dnn_predict(dnn, valid, errorMeasure));

    float trainAcc = 1.0f - (float) Ein / nTrain;

    if (trainAcc < 0) {
      cout << "."; cout.flush();
      continue;
    }

    float validAcc = 1.0f - (float) Eout[epoch] / nValid;

    printf("Epoch #%lu: Training Accuracy = %.4f %% ( %lu / %lu ), Validation Accuracy = %.4f %% ( %lu / %lu )\n",
      epoch, trainAcc * 100, nTrain - Ein, nTrain, validAcc * 100, nValid - Eout[epoch], nValid); 

    if (validAcc > dnn.getConfig().minValidAccuracy && isEoutStopDecrease(Eout, epoch, dnn.getConfig().nNonIncEpoch))
      break;

    dnn.adjustLearningRate(trainAcc);
  }

  // Show Summary
  printf("\n%ld epochs in total\n", epoch);
  timer.elapsed();

  printf("[   In-Sample   ] ");
  showAccuracy(Ein, train.size());
  printf("[ Out-of-Sample ] ");
  showAccuracy(Eout.back(), valid.size());
}

size_t dnn_predict(const DNN& dnn, DataSet& data, ERROR_MEASURE errorMeasure) {
  size_t nError = 0;

  Batches batches(2048, data.size());
  for (Batches::iterator itr = batches.begin(); itr != batches.end(); ++itr) {
    mat prob = dnn.feedForward(data.getX(*itr));
    nError += zeroOneError(prob, data.getY(*itr), errorMeasure);
  }

  return nError;
}

bool isEoutStopDecrease(const std::vector<size_t> Eout, size_t epoch, size_t nNonIncEpoch) {

  for (size_t i=0; i<nNonIncEpoch; ++i) {
    if (epoch - i > 0 && Eout[epoch] > Eout[epoch - i])
      return false;
  }

  return true;
}

