/* file: log_reg_binary_dense_online.cpp */
/*******************************************************************************
* Copyright 2020 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

/*
!  Content:
!    C++ example of logistic regression 2 classes in the online processing mode
!    with DPC++ interfaces.
!
!    The program trains the logistic regression model on a training
!    datasetFileName and computes classification for the test data.
!******************************************************************************/

/**
 * <a name="DAAL-EXAMPLE-CPP-LOG_REG_BINARY_DENSE_ONLINE"></a>
 * \example log_reg_binary_dense_online.cpp
 */

#include "logistic_regression_online.h"
#include "service.h"
#include "service_sycl.h"

using namespace daal;
using namespace daal::algorithms;
using namespace daal::algorithms::optimization_solver;
using namespace daal::data_management;

/* Input data set parameters */
const std::string trainDatasetFileName = "../data/batch/binary_cls_train.csv";
const std::string testDatasetFileName = "../data/batch/binary_cls_test.csv";
constexpr size_t nFeatures = 20;
constexpr size_t nClasses = 2;

logistic_regression::ModelPtr trainModel();
void testModel(const logistic_regression::ModelPtr& model);

int main(int argc, char* argv[]) {
    checkArguments(argc, argv, 2, &trainDatasetFileName, &testDatasetFileName);

    for (const auto& deviceSelector : getListOfDevices()) {
        const auto& nameDevice = deviceSelector.first;
        const auto& device = deviceSelector.second;
        sycl::queue queue(device);
        std::cout << "Running on " << nameDevice << "\n\n";

        services::SyclExecutionContext ctx(queue);
        services::Environment::getInstance()->setDefaultExecutionContext(ctx);

        auto model = trainModel();
        printNumericTable(model->getBeta(), "Logistic Regression coefficients:");

        testModel(model);
    }

    return 0;
}

logistic_regression::ModelPtr trainModel() {
    constexpr bool isIntercept = true;
    constexpr float l2Penalty = 0.1f;
    constexpr float learningRate = 1.0f;
    constexpr size_t nBlocksToProcess = 40;
    constexpr size_t nIterationsPerBlock = 5;
    constexpr size_t dataBlockRowCount = 1;

    auto xBlock = SyclHomogenNumericTable<>::create(nFeatures, 0, NumericTable::notAllocate);
    auto yBlock = SyclHomogenNumericTable<>::create(1, 0, NumericTable::notAllocate);
    NumericTablePtr mergedDataBlock(new MergedNumericTable(xBlock, yBlock));

    LogisticRegressionOnline alg;
    alg.setParams(nClasses,
                  nFeatures,
                  isIntercept,
                  l2Penalty,
                  dataBlockRowCount,
                  nIterationsPerBlock);

    FileDataSource<CSVFeatureManager> dataSource(trainDatasetFileName,
                                                 DataSource::notAllocateNumericTable,
                                                 DataSource::doDictionaryFromContext);

    size_t k = 1;
    while (dataSource.loadDataBlock(dataBlockRowCount, mergedDataBlock.get()) ==
               dataBlockRowCount &&
           k <= nBlocksToProcess) {
        alg.setIterationParams(learningRate / k);
        alg.setInput(xBlock, yBlock);
        alg.compute();
        k++;
    }

    alg.finalizeCompute();
    return alg.getModel();
}

void testModel(const logistic_regression::ModelPtr& model) {
    namespace class_prediction = classifier::prediction;
    using LogRegPrediction = logistic_regression::prediction::Batch<>;

    FileDataSource<CSVFeatureManager> dataSource(testDatasetFileName,
                                                 DataSource::notAllocateNumericTable,
                                                 DataSource::doDictionaryFromContext);

    /* Create Numeric Tables for training data and dependent variables */
    auto testX = SyclHomogenNumericTable<>::create(nFeatures, 0, NumericTable::notAllocate);
    auto testY = SyclHomogenNumericTable<>::create(1, 0, NumericTable::notAllocate);
    NumericTablePtr mergedData(new MergedNumericTable(testX, testY));

    /* Retrieve the data from input file */
    dataSource.loadDataBlock(mergedData.get());

    /* Create an algorithm object to predict values of logistic regression */
    LogRegPrediction algorithm(nClasses);

    /* Pass a testing data set and the trained model to the algorithm */
    algorithm.input.set(class_prediction::data, testX);
    algorithm.input.set(class_prediction::model, model);

    /* Predict values of logistic regression */
    algorithm.compute();

    /* Retrieve the algorithm results */
    auto predictionResult = algorithm.getResult();
    printNumericTable(predictionResult->get(class_prediction::prediction),
                      "Logistic regression prediction results (first 10 rows):",
                      10);
    printNumericTable(testY, "Ground truth (first 10 rows):", 10);
}
