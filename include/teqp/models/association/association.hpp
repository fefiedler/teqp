/**
General routines for the calculation of association
 
The implementation follows the approach of Langenbach for the index compression,
 
 Many helpful hints from Andres Riedemann
 
*/

#pragma once

#include <map>
#include <set>

#include "teqp/constants.hpp"
#include "teqp/types.hpp"

#include <Eigen/Dense>

#include "teqp/models/association/association_types.hpp"

namespace teqp{
namespace association{

struct AssociationOptions{
    std::map<std::string, std::vector<std::string>> interaction_partners;
    std::vector<std::string> site_order;
    association::radial_dist radial_dist;
};

/***
 A general class for calculating association fractions and association energy for mixtures
 
 A mixture is formed of multiple components, each component has an index i.
 For each component, it may multiple unique sites, and each unique site has a multiplicity associated with it.
 
 For instance, for 4C water, there are two "e" sites and two "H" sites in the nomenclature of Clapeyron.
 Thus the "e" site has multiplicity of 2 and the "H" site has multiplicity of 2
 */
class Association{
public:
    using CompSite = std::tuple<std::size_t, std::string>;
    using CompCIndex = std::tuple<std::size_t, std::size_t>;
    struct IndexMapper{
        std::map<std::size_t, CompSite> to_CompSite;
        std::map<CompSite, std::size_t> to_siteid;
        std::map<CompCIndex, std::size_t> CompCIndex_to_siteid;
        Eigen::ArrayXi counts; ///< An array of counts of each siteid for the entire mixture
        Eigen::ArrayXi N_sites; ///< How many total sites are on each molecule
        Eigen::ArrayXi N_unique_sites; ///< How many unique types of sites are on each molecule
        Eigen::ArrayXi comp_index; ///< The pure component indices associated with each siteid
        std::vector<std::vector<std::string>> molecule_sites;
    };
private:
    IndexMapper make_mapper(const std::vector<std::vector<std::string>>& molecule_sites, const AssociationOptions& options) const {
        IndexMapper ind;
        ind.counts.resize(1000);
        ind.comp_index.resize(1000);
        ind.N_sites.resize(molecule_sites.size());
        ind.N_unique_sites.resize(molecule_sites.size());
        ind.molecule_sites = molecule_sites;
                
        // Build the maps from siteid->(component index, site name) and (component index, site name)->siteid
        std::size_t icomp = 0;
        std::size_t siteid = 0;
        for (auto& molecule: molecule_sites){
            /// Count up how many times each site is present in the list of sites
            std::map<std::string, int> site_counts;
            for (auto& site: molecule){
                ++site_counts[site];
            }
            auto unique_sites_on_molecule = std::set(molecule.begin(), molecule.end());
            if (!options.site_order.empty()){
                // TODO: enforce sites to appear in the order matching the specification
                // TODO: this would be required to for instance check the D matrix of Langenbach and Enders
            }
            std::size_t Cindex = 0;
            for (auto& site: unique_sites_on_molecule){
                CompSite cs{icomp, site};
                ind.to_CompSite[siteid] = cs;
                ind.to_siteid[cs] = siteid;
                ind.CompCIndex_to_siteid[CompCIndex{icomp, Cindex}] = siteid;
                ind.counts[siteid] = site_counts[site];
                ind.comp_index[siteid] = static_cast<int>(icomp);
                Cindex++;
                siteid++;
            }
            ind.N_sites[icomp] = static_cast<int>(molecule.size());
            ind.N_unique_sites[icomp] = static_cast<int>(unique_sites_on_molecule.size());
            icomp++;
        }
        
        ind.counts.conservativeResize(siteid);
        ind.comp_index.conservativeResize(siteid);
        return ind;
    }
    
    /***
    Construct the counting matrix \f$ D_{IJ} \f$ as given by Langenbach and Enders
    */
    auto make_D(const IndexMapper& ind, const AssociationOptions& options ) const{
        
        auto get_DIJ = [&ind, &options](std::size_t I, std::size_t J) -> int {
            /** Return the value of an entry in the D_{IJ} matrix
            
            For a given unique site, look at all other sites on all other molecules
            */
            auto [ph1, typei] = ind.to_CompSite.at(I);
            auto [ph2, typej] = ind.to_CompSite.at(J);
            auto contains = [](auto& container, const auto& val){ return std::find(container.begin(), container.end(), val) != container.end(); };
            /// If interaction parameters are not provided, assume conservatively that all sites can interact with all other sites
            if (options.interaction_partners.empty() || (contains(options.interaction_partners.at(typei), typej))){
                return ind.counts[J];
            }
            return 0;
        };
        
        int Ngroups = static_cast<int>(ind.to_siteid.size());
        Eigen::ArrayXXi D(Ngroups, Ngroups);
        // I and J are the numerical indices of the sites
        for (int I = 0; I < Ngroups; ++I){
            for (int J = 0; J < Ngroups; ++J){
                D(I, J) = get_DIJ(I, J);
            }
        }
        return D;
    }
    
public:
    const Eigen::ArrayXd b_m3mol, ///< The covolume b, in m^3/mol
            beta, ///< The volume factor, dimensionless
            epsilon_Jmol; ///< The association energy of each molecule, in J/mol
    
    const IndexMapper mapper;
    const Eigen::ArrayXXi D;
    const radial_dist m_radial_dist;
    
    Association(const Eigen::ArrayXd& b_m3mol, const Eigen::ArrayXd& beta, const Eigen::ArrayXd& epsilon_Jmol, const std::vector<std::vector<std::string>>& molecule_sites, const AssociationOptions& options) : b_m3mol(b_m3mol), beta(beta), epsilon_Jmol(epsilon_Jmol), mapper(make_mapper(molecule_sites, options)), D(make_D(mapper, options)), m_radial_dist(options.radial_dist){
    }
    
    /**
        Build the Delta matrix, where entries are given by
     \f[
        \Delta_{IJ} = ...
     \f]
     */
    template<typename TType, typename RhoType, typename MoleFracsType>
    auto get_Delta(const TType& T, const RhoType& rhomolar, const MoleFracsType& molefracs) const {
        
        using resulttype = std::common_type_t<decltype(T), decltype(rhomolar), decltype(molefracs[0])>; // Type promotion, without the const-ness
        using Mat = Eigen::Array<resulttype, Eigen::Dynamic, Eigen::Dynamic>;
        auto Ngroups = mapper.to_CompSite.size();
        auto bmix = (molefracs*b_m3mol).sum();
        auto eta = bmix*rhomolar/4.0;
        
        decltype(forceeval(eta)) g;
        switch(m_radial_dist){
            case radial_dist::CS:
                g = (2.0-eta)/(2.0*(1.0-eta)*(1.0-eta)*(1.0-eta)); break;
            case radial_dist::KG:
                g = 1.0 / (1.0 - 1.9*eta); break;
            default:
                throw std::invalid_argument("Bad radial distribution");
        }
        
        Mat Delta = Mat::Zero(Ngroups, Ngroups);
        for (auto I = 0; I < Ngroups; ++I){
            auto i = std::get<0>(mapper.to_CompSite.at(I));
            for (auto J = 0; J < Ngroups; ++J){
                auto j = std::get<0>(mapper.to_CompSite.at(J));
                
                using namespace teqp::constants;
                // The CR1 rule is used to calculate the cross contributions
                if (true){ // Is CR1 // TODO: also allow the other combining rule
                    auto b_ij = (b_m3mol[i] + b_m3mol[j])/2.0;
                    auto epsilon_ij_Jmol = (epsilon_Jmol[i] + epsilon_Jmol[j])/2.0;
                    auto beta_ij = sqrt(beta[i]*beta[j]);
                    Delta(I, J) = g*b_ij*beta_ij*(exp(epsilon_ij_Jmol/(R_CODATA2017*T))-1.0)/N_A;
                }
            }
        }
        return Delta;
    }
    
    template<typename TType, typename RhoType, typename MoleFracsType, typename XType>
    auto successive_substitution(const TType& T, const RhoType& rhomolar, const MoleFracsType& molefracs, const XType& X_init) const {
        
        using resulttype = std::common_type_t<decltype(T), decltype(rhomolar), decltype(molefracs[0])>; // Type promotion, without the const-ness
        using Mat = Eigen::Array<resulttype, Eigen::Dynamic, Eigen::Dynamic>;
        
        const Mat Delta = get_Delta(T, rhomolar, molefracs);
//        const Mat DD = Delta.array()*D.cast<resulttype>().array(); // coefficient-wise product, with D upcast from int to type of Delta matrix
        
        auto Ngroups = mapper.to_CompSite.size();
        Eigen::RowVectorX<typename MoleFracsType::Scalar> xj(Ngroups); // Mole fractions of the component containing each siteid
        for (auto I = 0; I< Ngroups; ++I){
            xj(I) = molefracs(std::get<0>(mapper.to_CompSite.at(I)));
        }
        
        using rDDXtype = std::decay_t<std::common_type_t<typename decltype(Delta)::Scalar, decltype(rhomolar), decltype(molefracs[0])>>; // Type promotion, without the const-ness
        Eigen::MatrixX<rDDXtype> rDDX = rhomolar*N_A*(Delta.array()*D.cast<resulttype>().array()).matrix();
        for (auto j = 0; j < rDDX.rows(); ++j){
            rDDX.row(j).array() = rDDX.row(j).array()*xj.array();
        }
//        rDDX.rowwise() *= xj;
        
        Eigen::ArrayX<std::decay_t<rDDXtype>> X = X_init;
        int max_steps = 30;
        double alpha = 0.5;
        
        for (auto counter = 0; counter < max_steps; ++counter){
            X = alpha*X + (1.0-alpha)/(1.0+(rDDX*X.matrix()).array());
        }
        return X;
    }
    
    /**
     \brief Calculate the contribution \f$\alpha = a/(RT)\f$, where the Helmholtz energy \f$a\f$ is on a molar basis, making \f$\alpha\f$ dimensionless.
     */
    template<typename TType, typename RhoType, typename MoleFracsType>
    auto alphar(const TType& T, const RhoType& rhomolar, const MoleFracsType& molefracs) const {
        
        // Do the sucessive substitution to obtain the non-bonded fractions for each unique site
        Eigen::ArrayXd X_init = Eigen::ArrayXd::Ones(mapper.to_siteid.size());
        auto X_A = successive_substitution(T, rhomolar, molefracs, X_init);
        
        // Calculate the contribution alpha based on the "naive" summation like in Clapeyron
        using resulttype = std::common_type_t<decltype(T), decltype(rhomolar), decltype(molefracs[0])>; // Type promotion, without the const-ness
        resulttype alpha_r_asso = 0.0;
        for (auto icomp = 0; icomp < molefracs.size(); ++icomp){ // loop over all components
            resulttype summer = 0.0;
            for (auto jsite = 0; jsite < mapper.N_unique_sites[icomp]; ++jsite){
                // Get the siteid given the (component index, site index on the component)
                const auto I = mapper.CompCIndex_to_siteid.at({icomp, jsite});
                
                // each contribution (per unique site on a molecule) is multiplied by its multiplicity within the molecule
                summer += (log(X_A(I))- X_A(I)/2.0 + 0.5)*static_cast<double>(mapper.counts(I));
            }
            alpha_r_asso += molefracs[icomp]*summer;
        }
        return alpha_r_asso;
        
//        // And do the summation to calculate the contribution to alpha=a/(RT)
//        using V = std::common_type_t<double, decltype(X_A[0]), int>;
//        auto xj = molefracs(mapper.comp_index); // mole fractions of the components attached at each site
//        auto alpha_r_asso = (xj.template cast<V>()*(log(X_A) - X_A/2.0 + 0.5).template cast<V>()*mapper.counts.cast<double>()).sum();
//        return forceeval(alpha_r_asso);
    }
    
};

}
}
