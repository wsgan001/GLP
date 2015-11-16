//
//  SLSparsePls.cpp
//  GLP
//
//  Created by Zheng Shao on 12/28/12.
//  Copyright (c) 2012 Saigo Laboratoire. All rights reserved.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
//  02111-1307, USA
//

#include <Eigen/QR>
#include "SLSparsePls.h"
#include "../SLUtility.h"

using namespace std;

// Public Methods
SLModelResult SLSparsePls::train(const MatrixXd& appendedX, const MatrixXd& theY, SLMODELRESULTYPE type)
{
    if (Y.cols() == 0 || Y.rows() == 0)
    {
        Y = theY;
        Res = Y;
        
        if (param.mode == PLSMODECLA)
        {
            min = Y.minCoeff();
            max = Y.maxCoeff();
            
            double eps = 1e-4;
            ASSERT((Y.array() > min+eps && Y.array() < max-eps).any() == false,
                   "Only binary label was supported in classification mode");
        }
    }
    
    ssize_t appendedXRows = appendedX.rows();
    ssize_t appendedXCols = appendedX.cols();
    ssize_t oldXCols      = X.cols();
       
    ssize_t selectedColIndex;
    VectorXd selectedCol;
    if(param.colMode == PLSCOLSELVAR)
    {
        ColVariance(Res).maxCoeff(&selectedColIndex);
        selectedCol = Res.col(selectedColIndex);
    }
    else if(param.colMode == PLSCOLSELRAND)
    {
        selectedCol = Res.col(param.randIndex);
    }
    else if(param.colMode == PLSCOLSELAVG)
    {
        VectorXd ResMean(Res.rows());
        ResMean.setZero();
        
        for (ssize_t i=0; i<Res.cols(); ++i)
            ResMean += Res.col(i);
        selectedCol = ResMean/Res.cols();
    }
    X.conservativeResize(appendedXRows, oldXCols+appendedXCols);
    X.rightCols(appendedXCols).setZero();
    X << X.leftCols(oldXCols), appendedX;
    
    W.conservativeResize(oldXCols+appendedXCols, W.cols()+1);
    W.bottomRows(appendedXCols).setZero();
    W.rightCols(1).setZero();
    
    W.rightCols(1) = X.transpose()*selectedCol;
    W.rightCols(1).normalize();
        
    T.conservativeResize(appendedXRows, T.cols()+1);
    T.rightCols(1).setZero();
    
    if(T.cols() > 1)
    {
        size_t oldTCols = (size_t)(T.cols()-1);
        T.rightCols(1) = (MatrixXd::Identity(X.rows(),X.rows()) - T.leftCols(oldTCols)*(T.leftCols(oldTCols).transpose())) * X * W.rightCols(1);
    }
    else
        T.rightCols(1) = X * W.rightCols(1);
    
    T.rightCols(1).normalize();
    
    Beta = W*(W.transpose()*X.transpose()*X*W).colPivHouseholderQr().solve(W.transpose()*X.transpose()*Y);
    
    if (param.verbose)
    {
        LOG(Y);
        LOG(X.rightCols(appendedXCols));
        LOG(W);
        LOG(selectedCol);
        LOG(Beta);
        LOG(X*Beta.col(0));
        getchar();
    }
    Res = Y - X*Beta;
    
    return getTrainResult(type);
}

SLModelResult SLSparsePls::classify(const MatrixXd& tX, const MatrixXd& tY, SLMODELRESULTYPE type) const
{
    ASSERT(type != SLModelResultTypeNone, "No type of result was indicated");
    
    ASSERT(!(type & ~(SLModelResultTypeBeta |
                      SLModelResultTypeQ2   |
                      SLModelResultTypeRSS  |
                      SLModelResultTypeACC  |
                      SLModelResultTypeAUC  |
                      SLModelResultTypeAIC  |
                      SLModelResultTypeBIC  |
                      SLModelResultTypeCOV)),
           "Only support Beta Q2 RSS ACC AUC AIC BIC COV for Sparse PLS.");
    
    ASSERT(Beta.cols() != 0 && Beta.rows() != 0, "Train data first");
    
    SLModelResult result;
    
    MatrixXd tRES = tY - tX*Beta;

    if(type & SLModelResultTypeQ2)
    {
        result[SLModelResultTypeQ2] = getQ2(tRES, tY);
    }
    
    if(type & SLModelResultTypeRSS)
    {
        result[SLModelResultTypeRSS] = getRSS(tRES);
    }
    
    if(type & SLModelResultTypeBeta)
    {
        result[SLModelResultTypeBeta] = (MatrixXd)(Beta);
    }
    
    if(type & SLModelResultTypeACC)
    {
        result[SLModelResultTypeACC] = getACC(tY, tX*Beta);
    }
    
    if(type & SLModelResultTypeAUC)
    {
        result[SLModelResultTypeAUC] = getAUC(tY, tX*Beta);
    }
    
    if(type & SLModelResultTypeAIC)
    {
        result[SLModelResultTypeAIC] = getAIC(tY, tX*Beta, X.cols());
    }

    if(type & SLModelResultTypeBIC)
    {
        result[SLModelResultTypeBIC] = getBIC(tY, tX*Beta, X.cols());
    }

    if(type & SLModelResultTypeCOV)
    {
        result[SLModelResultTypeCOV] = getCOV(tY, tX*Beta);
    }
    return result;
}

bool SLSparsePls::setParameters(SLSparsePlsParameters& parameters)
{
    param = parameters;
    return true;
}

// Private Methods
SLModelResult SLSparsePls::getTrainResult(SLMODELRESULTYPE type) const
{
    ASSERT(!(type & ~(SLModelResultTypeBeta |
                      SLModelResultTypeQ2   |
                      SLModelResultTypeRSS  |
                      SLModelResultTypeACC  |
                      SLModelResultTypeAUC  |
                      SLModelResultTypeAIC  |
                      SLModelResultTypeBIC  |
                      SLModelResultTypeCOV)),
           "Only support Beta Q2 RSS ACC AUC AIC BIC COV for Sparse PLS.");
    
    ASSERT(Beta.cols() != 0 && Beta.rows() != 0, "Train data first");
    
    SLModelResult result;
    if(type & SLModelResultTypeQ2)
    {
        result[SLModelResultTypeQ2] = getQ2(Res, Y);
    }
    
    if(type & SLModelResultTypeRSS)
    {
        result[SLModelResultTypeRSS] = getRSS(Res);
    }
    
    if(type & SLModelResultTypeBeta)
    {
        result[SLModelResultTypeBeta] = (MatrixXd)Beta;
    }
    
    if(type & SLModelResultTypeACC)
    {
        result[SLModelResultTypeACC] = getACC(Y, X*Beta);
    }
    
    if(type & SLModelResultTypeAUC)
    {
        result[SLModelResultTypeAUC] = getAUC(Y, X*Beta);
    }
    
    if(type & SLModelResultTypeAIC)
    {
        result[SLModelResultTypeAIC] = getAIC(Y, X*Beta, X.cols());
    }
    
    if(type & SLModelResultTypeBIC)
    {
        result[SLModelResultTypeBIC] = getBIC(Y, X*Beta, X.cols());
    }

    if(type & SLModelResultTypeCOV)
    {
        result[SLModelResultTypeCOV] = getCOV(Y, X*Beta);
    }
    return result;
}