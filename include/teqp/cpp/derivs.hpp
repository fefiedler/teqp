#pragma once

namespace teqp{
namespace cppinterface{

/**
 A small data holding class for the Jacobian and values obtained for a set of thermodynamic properties
 */
struct IterationMatrices{
    std::vector<char> vars; ///< The set of variables matching the rows in the Jacobian
    Eigen::ArrayXXd J; ///< The Jacobian
    Eigen::ArrayXd v; ///< The values of the thermodynamic variables matchign the variables in vars
};

/**
 \brief A convenience function for calculation of Jacobian terms of the form \f$ J_{i0} = \frac{\partial y}{\partial T} \f$  and \f$ J_{i1} = \frac{\partial y}{\partial \rho} \f$ where $y$ is one of the thermodynamic variables in vars
 
 \param vars A set of chars, allowed are 'H','S','U','P','T','D'
 \param Ar The matrix of derivatives of $\alpha^{\rm r}, perhaps obtained from teqp::DerivativeHolderSquare, or via get_deriv_mat2 of the AbstractModel
 \param Aig The matrix of derivatives of $\alpha^{\rm ig}, perhaps obtained from teqp::DerivativeHolderSquare, or via get_deriv_mat2 of the AbstractModel
 \param R The molar gas constant
 \param T Temperature
 \param rho Molar density
 \param z Mole fractions
 */
template<typename Array>
auto build_iteration_Jv(const std::vector<char>& vars, const Eigen::Array<double, 3, 3>& Ar, const Eigen::Array<double, 3, 3>& Aig, const double R, const double T, const double rho, const Array &z){
    IterationMatrices im; im.J.resize(vars.size(), 2); im.v.resize(vars.size()); im.vars = vars;
    
    auto A = Ar + Aig;
    Eigen::ArrayXd& v = im.v;
    Eigen::ArrayXXd& J = im.J;
    auto Trecip = 1.0/T;
    auto dTrecipdT = -Trecip*Trecip;
    
    // Define some lambda functions for things that *might* be needed
    // The lambdas are used to get a sort of lazy evaluation. The lambdas are
    // only evaluated on an as-needed basis.  If the lambda is not called,
    // no cost is incurred.
    //
    // Probably the compiler will inline these functions anyhow.
    //
    // Derivatives of total alpha; alpha = a/(R*T) = a*R/Trecip
    auto alpha = [&](){ return A(0,0); };
    auto dalphadTrecip = [&](){ return A(1,0)/Trecip; };
    auto dalphadrho = [&](){ return A(0,1)/rho; };
    auto d2alphadTrecip2 = [&](){ return A(2,0)/(Trecip*Trecip); };
    auto d2alphadTrecipdrho = [&](){ return A(1,1)/(Trecip*rho); };
    auto d2alphadrho2 = [&](){ return A(0,2)/(rho*rho); };
    //
    // Derivatives of total Helmholtz energy a in terms of derivatives of alpha
    auto dadTrecip = [&](){ return R/(Trecip*Trecip)*(Trecip*dalphadTrecip()-alpha());};
    auto d2adTrecip2 = [&](){ return R/(Trecip*Trecip*Trecip)*(Trecip*Trecip*d2alphadTrecip2()-2*Trecip*dalphadTrecip()+2*alpha());};
    auto dadrho = [&](){return R/Trecip*(dalphadrho());};
    auto d2adrho2 = [&](){return R/Trecip*(d2alphadrho2());};
    auto d2adTrecipdrho = [&](){ return R/(Trecip*Trecip)*(Trecip*d2alphadTrecipdrho()-dalphadrho());};
    
    for (auto i = 0; i < vars.size(); ++i){
        switch(vars[i]){
            case 'T':
                v(i) = T;
                J(i, 0) = 1.0;
                J(i, 1) = 0.0;
                break;
            case 'D':
                v(i) = rho;
                J(i, 0) = 0.0;
                J(i, 1) = 1.0;
                break;
            case 'P':
                v(i) = rho*R*T*(1 + Ar(0,1));
                J(i, 0) = rho*R*(1 + Ar(0,1) - Ar(1,1));
                J(i, 1) = R*T*(1 + 2*Ar(0,1) + Ar(0,2));
                break;
            case 'S':
                v(i) = Trecip*Trecip*dadTrecip();
                J(i, 0) = (Trecip*Trecip*d2adTrecip2() + 2*Trecip*dadTrecip())*dTrecipdT;
                J(i, 1) = Trecip*Trecip*d2adTrecipdrho();
                break;
            default:
                throw std::invalid_argument("bad var: " + std::to_string(vars[i]));
        }
    }
    return im;
}

}
};