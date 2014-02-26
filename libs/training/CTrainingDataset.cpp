
/*---------------------------------------------------------------------------*
 |                               UPGM++                                      |
 |                   Undirected Graphical Models in C++                      |
 |                                                                           |
 |              Copyright (C) 2014 Jose Raul Ruiz Sarmiento                  |
 |                 University of Malaga (jotaraul@uma.es)                    |
 |                         University of Osnabruk                            |
 |                                                                           |
 |   This program is free software: you can redistribute it and/or modify    |
 |   it under the terms of the GNU General Public License as published by    |
 |   the Free Software Foundation, either version 3 of the License, or       |
 |   (at your option) any later version.                                     |
 |                                                                           |
 |   This program is distributed in the hope that it will be useful,         |
 |   but WITHOUT ANY WARRANTY; without even the implied warranty of          |
 |   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           |
 |   GNU General Public License for more details.                            |
 |   <http://www.gnu.org/licenses/>                                          |
 |                                                                           |
 *---------------------------------------------------------------------------*/

#include "CTrainingDataset.hpp"
#include <lbfgs.h>

#include <vector>

using namespace std;
using namespace UPGMpp;


/*------------------------------------------------------------------------------

                             updateNodeTypeWeights

------------------------------------------------------------------------------*/

void updateNodeTypeWeights( Eigen::MatrixXi & nodeWeightsMap,
                        CNodeTypePtr nodeType,
                        const double *x )
{
    Eigen::MatrixXd &weights = nodeType->getWeights();

    size_t N_cols = weights.cols();
    size_t N_rows = weights.rows();

    for ( size_t row = 0; row < N_rows; row++ )
        for ( size_t col = 0; col < N_cols; col++ )
        {
            weights(row,col) = x[nodeWeightsMap(row,col)];
        }
}


/*------------------------------------------------------------------------------

                              updateEdgeTypeWeights

------------------------------------------------------------------------------*/

void updateEdgeTypeWeights( std::vector<Eigen::MatrixXi>& edgeWeightsMap,
                        CEdgeTypePtr  edgeType,
                        const double *x)
{
    std::vector<Eigen::MatrixXd> &weights = edgeType->getWeights();
    for ( size_t feature = 0; feature < weights.size(); feature++ )
    {
        size_t N_cols = weights[feature].cols();
        size_t N_rows = weights[feature].rows();

        for ( size_t row = 0; row < N_rows; row++ )
            for ( size_t col = 0; col < N_cols; col++ )
            {
                weights[feature](row,col) = x[edgeWeightsMap[feature](row,col)];
            }
    }

}

/*------------------------------------------------------------------------------

                                evaluate

------------------------------------------------------------------------------*/

static lbfgsfloatval_t evaluate(
    void *instance,
    const lbfgsfloatval_t *x,
    lbfgsfloatval_t *g,
    const int n,
    const lbfgsfloatval_t step
    )
{

    using namespace UPGMpp;
    CTrainingDataSet *td = static_cast<UPGMpp::CTrainingDataSet*>(instance);

    // Reset the vector of gradients
    for ( size_t i = 0; i < n; i++ )
        g[i] = 0;

    // Update the node and edge weights
    vector<CNodeTypePtr> &nodeTypes = td->getNodeTypes();
    vector<CEdgeTypePtr> &edgeTypes = td->getEdgeTypes();

    size_t N_nodeTypes = nodeTypes.size();
    size_t N_edgeTypes = edgeTypes.size();

    for ( size_t index = 0; index < N_nodeTypes; index++ )
        updateNodeTypeWeights( td->getCertainNodeWeightsMap( nodeTypes[index] ),
                           nodeTypes[index],
                           x);

    for ( size_t index = 0; index < N_edgeTypes; index++ )
        updateEdgeTypeWeights( td->getCertainEdgeWeightsMap( edgeTypes[index] ),
                           edgeTypes[index],
                           x);

    // For each graph in the dataset

   /* for ( size_t i=0; i < n; i++ )
        cout << "x[" << i << "] : " << x[i] << endl;*/

    lbfgsfloatval_t fx = 0.0;


    vector<CGraph> & graphs = td->getGraphs();
    std::vector<std::map<size_t,size_t> > &groundTruth = td->getGroundTruth();

    size_t N_datasets = graphs.size();

    // Compute the function value

    for ( size_t dataset = 0; dataset < N_datasets; dataset++ )
    {        
        graphs[dataset].computePotentials();
        td->updateFunctionValueAndGradients( graphs[dataset], groundTruth[dataset], fx, x, g );
    }

    double lambda = 10;

    // Apply L-2 norm
    double regularizationFactor = 0;
    for ( size_t i = 0; i < n; i++ )
    {
        regularizationFactor += lambda*(x[i]*x[i]);
        g[i] += 2*lambda*x[i];
    }

    fx = fx + regularizationFactor;

    //cout << "Regularized FX: " << fx << endl;

    return fx;
}

/*------------------------------------------------------------------------------

                                progress

------------------------------------------------------------------------------*/

static int progress(
    void *instance,
    const lbfgsfloatval_t *x,
    const lbfgsfloatval_t *g,
    const lbfgsfloatval_t fx,
    const lbfgsfloatval_t xnorm,
    const lbfgsfloatval_t gnorm,
    const lbfgsfloatval_t step,
    int n,
    int k,
    int ls
    )
{
    cout << "Iteration " << k << endl;
    cout << "  fx = " <<  fx << ", x[0] = " << x[0] << ", x[1] = " << x[1] << endl;
    cout << "  xnorm = " << xnorm << ", gnorm = " << gnorm << ", step = " << step << endl << endl;

    return 0;
}


/*------------------------------------------------------------------------------

                                train

------------------------------------------------------------------------------*/

void CTrainingDataSet::train()
{
    // Steps of the train algorithm:
    //  1. Build the mapping between the different types of nodes and edges and
    //      the positions of the vector of weights to optimize.
    //  2. Initialize the weights.
    //  3. Configure the parameters of the optimization method.
    //  4. Launch optimization (training).
    //  5. Show optimization results.

    //
    //  1. Build the mapping between the different types of nodes and edges and
    //      the positions of the vector of weights to optimize.
    //

    // Nodes

    N_weights = 0;
    for ( size_t i = 0; i < m_nodeTypes.size(); i++ )
    {
        size_t N_cols = m_nodeTypes[i]->getWeights().cols();
        size_t N_rows = m_nodeTypes[i]->getWeights().rows();

        Eigen::MatrixXi weightsMap;
        weightsMap.resize( N_rows, N_cols );

        size_t index = N_weights;

        for ( size_t row = 0; row < N_rows; row++)
            for ( size_t col = 0; col < N_cols; col++ )
            {
                weightsMap(row,col) = index;
                index++;
            }

        m_nodeWeightsMap[m_nodeTypes[i]] = weightsMap;

        N_weights += N_cols * N_rows;

        //cout << *m_nodeTypes[i] << endl;

    }

    // Edges

    for ( size_t i = 0; i < m_edgeTypes.size(); i++ )
    {
        size_t N_features = m_edgeTypes[i]->getWeights().size();

        std::vector<Eigen::MatrixXi> v_weightsMap(N_features);

        size_t index = N_weights;

        Eigen::VectorXi typeOfEdgeFeatures =
                                          m_typesOfEdgeFeatures[m_edgeTypes[i]];

        for ( size_t feature = 0; feature < N_features; feature++ )
        {
            if ( typeOfEdgeFeatures(feature) == 0)
            {
                size_t N_cols = m_edgeTypes[i]->getWeights()[feature].cols();
                size_t N_rows = m_edgeTypes[i]->getWeights()[feature].rows();

                Eigen::MatrixXi weightsMap;
                weightsMap.resize( N_rows, N_cols );

                weightsMap(0,0) = index;

                for ( size_t row = 0; row < N_rows; row++ ) // rows
                {
                    index++;

                    for ( size_t col = row+1; col < N_cols; col++ )  // cols
                    {
                        weightsMap(row,col) = index;
                        weightsMap(col,row) = index;
                        index++;
                    }
                }

                size_t previousW = N_weights;

                for ( size_t c3 = 1; c3 < N_rows; c3++ )
                {
                    index = previousW + (N_rows - c3) + 1;

                    weightsMap(c3,c3) = index;
                    previousW = index;
                }

                index++;

                N_weights = weightsMap(N_rows-1,N_cols-1)+1;

                v_weightsMap[feature] = weightsMap;

            }
            else if ( typeOfEdgeFeatures(feature) == 1 )
            {
                size_t N_cols = m_edgeTypes[i]->getWeights()[feature].cols();
                size_t N_rows = m_edgeTypes[i]->getWeights()[feature].rows();


                Eigen::MatrixXi weightsMap;
                weightsMap.resize( N_rows, N_cols );

                for ( size_t row = 0; row < N_rows; row++)
                    for ( size_t col = 0; col < N_cols; col++ )
                    {
                        weightsMap(row,col) = index;
                        index++;
                    }

                v_weightsMap[feature] = weightsMap;

                N_weights += N_cols * N_rows;
            }
            else if ( typeOfEdgeFeatures(feature) == 2 )
            {
                v_weightsMap[feature] = v_weightsMap[feature-1].transpose();
            }

            cout << "Map for feature" << feature << endl;
            cout << v_weightsMap[feature] << endl;
        }

        m_edgeWeightsMap[m_edgeTypes[i]] = v_weightsMap;

        //cout << *m_edgeTypes[i] << endl;
    }

    cout << "Number of weights" << N_weights << endl;
    //
    //  2. Initialize the weights.
    //

    // Initialize weights
    lbfgsfloatval_t *x = lbfgs_malloc(N_weights);

    for ( size_t i = 0; i < N_weights; i++ )
        x[i] = 0;

    //
    //  3. Configure the parameters of the optimization method.
    //

    lbfgsfloatval_t fx;
    lbfgs_parameter_t param;


    if (x == NULL) {
        cout << "ERROR: Failed to allocate a memory block for variables." << endl;
    }

    /* Initialize the parameters for the L-BFGS optimization. */
    //param.orthantwise_c = 100;
    //param.orthantwise_start = 1;
    //param.orthantwise_end = N_weights - 1;
    param.linesearch = LBFGS_LINESEARCH_BACKTRACKING;
    lbfgs_parameter_init(&param);

    //
    //  4. Launch optimization (training).
    //

    /*
        Start the L-BFGS optimization; this will invoke the callback functions
        evaluate() and progress() when necessary.
     */

    int ret = lbfgs(N_weights, x, &fx, evaluate, progress, this, &param);

    //
    //  5. Show optimization results.
    //

    if (ret == LBFGS_CONVERGENCE) {
        cout << "L-BFGS resulted in convergence" << endl;
    } else if (ret == LBFGS_STOP) {
        cout << "L-BFGS terminated with the stopping criteria" << endl;
    } else if (ret == LBFGSERR_MAXIMUMITERATION) {
        cout << "L-BFGS terminated with the maximum number of iterations" << endl;
    } else {
        cout << "L-BFGS terminated with error code : " << ret << endl;
    }

    cout << "  fx = " << fx << ", x[0] = " << x[0] << ", x[1] = " << x[1] << endl;

    cout << "Final x " << endl;
    for ( size_t i=0; i < N_weights; i++ )
            cout << "x[" << i << "] : " << x[i] << endl;

    for ( size_t i = 0; i < m_edgeTypes.size(); i++ )
    {
        cout << "Edge type " << m_edgeTypes[i]->getID() << endl;
        for ( size_t feat = 0; feat < m_edgeTypes[i]->getWeights().size(); feat++)
            cout << "Feature " << feat << " weights: " << endl << m_edgeTypes[i]->getWeights()[feat] << endl;

        cout << "----------------------------------------------" << endl;
    }

    lbfgs_free(x);
}


/*------------------------------------------------------------------------------

                        updateFunctionValueAndGradients

------------------------------------------------------------------------------*/

void CTrainingDataSet::updateFunctionValueAndGradients( CGraph &graph,
                                                        std::map<size_t,size_t> &groundTruth,
                                                        lbfgsfloatval_t &fx,
                                                        const lbfgsfloatval_t *x,
                                                        lbfgsfloatval_t *g )
{

    //cout << "[STATUS] Updating function value and grandients!" << endl ;
    vector<CNodePtr> &nodes = graph.getNodes();

    vector<CNodePtr>::iterator itNodes;

    // Computet the probability of each class of each node wile they neighbors
    // take a fixed value
    for ( itNodes = nodes.begin(); itNodes != nodes.end(); itNodes++ )
    {
        CNodePtr node = *itNodes;

        // Get direct access to some interesting members of the node
        CNodeTypePtr    nodeType    = node->getType();
        Eigen::VectorXd potentials  = node->getPotentials();
        const Eigen::VectorXd &features = node->getFeatures();

        size_t ID = node->getId();

        // Multiply the node potentias with the
        pair<multimap<size_t,CEdgePtr>::iterator,multimap<size_t,CEdgePtr>::iterator > neighbors;

        neighbors = graph.getEdgesF().equal_range(ID);

        for ( multimap<size_t,CEdgePtr>::iterator it = neighbors.first; it != neighbors.second; it++ )
        {
            size_t neighborID;
            size_t ID1, ID2;

            CEdgePtr        edgePtr ((*it).second);
            Eigen::MatrixXd edgePotentials = edgePtr->getPotentials();

            edgePtr->getNodesID(ID1,ID2);

            if ( ID1 == ID ) // The neighbor node indexes the columns
                potentials = potentials.cwiseProduct(
                                edgePotentials.col( groundTruth[ID2] )
                                );
            else // The neighbor node indexes the rows
                potentials = potentials.cwiseProduct(
                                edgePotentials.row( groundTruth[ID1] ).transpose()
                                );
        }

        // Update objective funciton value!!!
        fx = fx - std::log( potentials(groundTruth[ID]) ) + std::log( potentials.sum() );

        // Update gradient

        //cout << "****************************************" << endl;
        //cout << "Potentials    : " << potentials.transpose() << endl;
        //cout << "Potentials sum:" << potentials.sum() << endl;
        Eigen::VectorXd nodeBel = potentials * ( 1 / (double) potentials.sum() );
        //cout << "Node bel      : " << nodeBel.transpose() << endl;

        size_t N_classes = potentials.rows();
        size_t N_features = features.rows();

        //cout << "N_Classes" << N_classes << " N_Features: " << N_features << endl;

        // Update node weights gradient
        for ( size_t class_i = 0; class_i < N_classes; class_i++ )
        {
            for ( size_t feature = 0; feature < N_features; feature++ )
            {
                size_t index = m_nodeWeightsMap[nodeType](class_i,feature);
                //cout << "Class" << class_i << " Feature: " << feature << endl;

                if ( index > 0 ) // is the weight set to 0?
                {
                    double ok = 0;

                    if ( class_i == groundTruth[ID] )
                        ok = 1;



                    //cout << "----------------------------------------------" << endl;

                    //cout << "Previous g[" << index << "]" << g[index] << endl;

                    g[index] = g[index] + features(feature)*(nodeBel(class_i) - ok);

                    //cout << "Gradient at g[" << index << "]: " << g[index] << endl;

                    //cout << "Feature va lue: " << features(feature) << endl;
                    //cout << "Node bel     : " << nodeBel(feature) << endl;
                    //cout << "Ok           : " << ok << endl;
                }

            }
        }

        // Update the gradients of its edges weights
        for ( multimap<size_t,CEdgePtr>::iterator it = neighbors.first; it != neighbors.second; it++ )
        {
            size_t ID1, ID2;

            CEdgePtr        edgePtr = (*it).second;
            CEdgeTypePtr    edgeTypePtr = edgePtr->getType();
            size_t          N_edgeFeatures = edgePtr->getFeatures().rows();

            edgePtr->getNodesID(ID1,ID2);

            size_t rowsIndex, colsIndex;


            //cout << "N_classes : " << N_classes << endl;
            //cout << "N_edgeFeatures : " << N_edgeFeatures << endl;

            for ( size_t class_i = 0; class_i < N_classes; class_i++ )
            {
                if ( ID1 == ID )
                {
                    rowsIndex = class_i;
                    colsIndex = groundTruth[ID2];
                }
                else
                {
                    rowsIndex = groundTruth[ID1];
                    colsIndex = class_i;
                }

                for ( size_t feature = 0; feature < N_edgeFeatures; feature++ )
                {
                    size_t index;

                    index = m_edgeWeightsMap[edgeTypePtr][feature](rowsIndex,colsIndex);

                    if ( index > 0 )
                    {
                        double ok = 0;
                        if ( class_i == groundTruth[ID] )
                            ok = 1;

                        //cout << "Index: " << index << " Class index: " << class_i << " nodeBel: " << nodeBel.transpose() << endl;

                        g[index] = g[index] +
                                edgePtr->getFeatures()[feature] *
                                ( nodeBel(class_i) - ok );
                    }


                    //cout << "----------------------------------------------" << endl;
                    //cout << "Row index " << rowsIndex << " cols index " << colsIndex  << endl;
                    //cout << "Gradient at " << index <<": " << g[index] << endl;

                }
            }
        }
    }

    //cout << "Fx: " << fx << endl;
    //cout << "[STATUS] Function value and gradients updated!" << endl;

}