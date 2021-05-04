#pragma once

template<typename X> auto POW2(X x) { return x * x; };
template<typename X> auto POW3(X x) { return x * POW2(x); };

enum class association_classes {not_set, a1A, a2B, a3B, a4C, not_associating};
enum class radial_dist { CS, KG, OT };

/// Function that calculates the association binding strength between site A of molecule i and site B on molecule j
template<typename BType, typename TType, typename RhoType, typename VecType>
auto get_DeltaAB_pure(radial_dist dist, double epsABi, double betaABi, BType b_cubic, TType T, RhoType rhomolar, const VecType& molefrac) {

    using eta_type = std::common_type_t<decltype(rhomolar), decltype(b_cubic)>;
    eta_type eta;
    eta_type g_vm_ref;

    // Calculate the contact value of the radial distribution function g(v)
    switch (dist) {
        case radial_dist::CS: {
            // Carnahan - Starling EOS, given by Kontogeorgis et al., Ind.Eng.Chem.Res. 2006, 45, 4855 - 4868, Eq. 4a and 4b:
            eta = (rhomolar / 4.0) * b_cubic;
            g_vm_ref = (2.0 - eta) / (2.0 * POW3(1.0 - eta));
            break;
        }
        case radial_dist::KG: {
            // Function proposed by  Kontogeorgis, G.M.; Yakoumis, I.V.; Meijer, H.; Hendriks, E.M.; Moorwood, T., Fluid Phase Equilib. 1999, 158 - 160, 201.
            eta = (rhomolar / 4.0) * b_cubic;
            g_vm_ref = 1.0 / (1.0 - 1.9 * eta);
            break;
        }
        case radial_dist::OT: {
            g_vm_ref = 1.0 / (1.0 - 0.475 * rhomolar * b_cubic);
            break;
        }
        default: {
            throw std::invalid_argument("Bad radial_dist");
        }
    }
    double R_gas = 8.3144598;

    // Calculate the association strength between site Ai and Bi for a pure compent
    auto DeltaAiBj = forceeval(g_vm_ref*(exp(epsABi /(T*R_gas)) - 1.0)*b_cubic* betaABi);

    return DeltaAiBj;
};

/// Routine that calculates the fractions of sites Ai not bound to other active sites for pure fluids
/// Some association schemes are explicitly solvable for self - associating compounds, see Huang and Radosz, Ind.Eng.Chem.Res., 29 (11), 1990
/// So far implemented association schemes : 1A, 2B, 3B, 4C (see Kontogeorgis et al., Ind. Eng. Chem. Res. 2006, 45, 4855 - 4868)
/// 

template<typename BType, typename TType, typename RhoType, typename VecType>
auto XA_calc_pure(int N_sites, association_classes scheme, double epsABi, double betaABi, const BType b_cubic, const TType T, const RhoType rhomolar, const VecType& molefrac) {

    // Matrix XA(A, j) that contains all of the fractions of sites A not bonded to other active sites for each molecule i
    // Start values for the iteration(set all sites to non - bonded, = 1)
    Eigen::Array<RhoType, Eigen::Dynamic, Eigen::Dynamic> XA;  // A maximum of 4 association sites(A, B, C, D)
    XA.resize(N_sites, 1);
    XA.setOnes();

    // Get the association strength between the associating sites
    auto dist = radial_dist::KG; // TODO: pass this in
    auto DeltaAiBj = get_DeltaAB_pure(dist, epsABi, betaABi, b_cubic, T, rhomolar, molefrac);

    if (scheme == association_classes::a1A) { // Acids
        // Only one association site "A"  (OH - group with C = O - group)
        XA(0, 0) = forceeval((-1.0 + sqrt(1.0 + 4.0 * rhomolar * DeltaAiBj)) / (2.0 * rhomolar * DeltaAiBj));
    }
    else if (scheme == association_classes::a2B) { // Alcohols
        // Two association sites "A" and "B"
        XA(0, 0) = forceeval((-1.0 + sqrt(1.0 + 4.0 * rhomolar * DeltaAiBj)) / (2.0 * rhomolar * DeltaAiBj));
        XA(1, 0) = XA(0, 0);   // XB = XA;
    }
    else if (scheme == association_classes::a3B) { // Glycols
        // Three association sites "A", "B", "C"
        XA(0, 0) = forceeval((-(1.0 - rhomolar * DeltaAiBj) + sqrt(POW2(1.0 + rhomolar * DeltaAiBj) + 4.0 * rhomolar * DeltaAiBj)) / (4.0 * rhomolar * DeltaAiBj));
        XA(1, 0) = XA(0, 0);         // XB = XA
        XA(2, 0) = 2 * XA(0, 0) - 1; // XC = 2XA - 1
    }
    else if (scheme == association_classes::a4C) { // Water
        // Four association sites "A", "B", "C", "D"
        XA(0, 0) = forceeval((-1.0 + sqrt(1.0 + 8.0 * rhomolar * DeltaAiBj)) / (4.0 * rhomolar * DeltaAiBj));
        XA(1, 0) = XA(0, 0);   // XB = XA
        XA(2, 0) = XA(0, 0);   // XC = XA
        XA(3, 0) = XA(0, 0);   // XD = XA
    }
    else if (scheme == association_classes::not_associating) { // non - associating compound
        XA(0, 0) = 1;
        XA(1, 0) = 1;
        XA(2, 0) = 1;
        XA(3, 0) = 1;
    }
    else {
        throw std::invalid_argument("Bad scheme");
    }
    return XA;
};

enum class cubic_flag {not_set, PR, SRK};

class CPACubic {
private:

    std::valarray<double> a0, bi, c1, Tc;
    double delta_1, delta_2;
    std::valarray<std::valarray<double>> k_ij;

public:
    CPACubic(cubic_flag flag, const std::valarray<double> a0, const std::valarray<double> bi, const std::valarray<double> c1, const std::valarray<double> Tc) : a0(a0), bi(bi), c1(c1), Tc(Tc) {
        switch (flag) {
        case cubic_flag::PR:
        { delta_1 = 1 + sqrt(2); delta_2 = 1 - sqrt(2); break; }
        case cubic_flag::SRK:
        { delta_1 = 0; delta_2 = 1; break; }
        default:
            throw std::invalid_argument("Bad cubic flag");
        }
        k_ij.resize(Tc.size()); for (auto i = 0; i < k_ij.size(); ++i) { k_ij[i].resize(Tc.size()); }
    };

    template<typename TType>
    auto get_ai(TType T, int i) const {
        return a0[i] * POW2(1 + c1[i]*(1 - sqrt(T / Tc[i])));
    }

    template<typename TType, typename VecType>
    auto get_ab(const TType T, const VecType& molefrac) const {
        using return_type = std::common_type_t<decltype(T), decltype(molefrac[0])>;
        return_type asummer = 0.0, bsummer = 0.0;
        for (auto i = 0; i < molefrac.size(); ++i) {
            bsummer += molefrac[i] * bi[i];
            auto ai = get_ai(T, i);
            for (auto j = 0; j < molefrac.size(); ++j) {
                auto aj = get_ai(T, j);
                auto a_ij = (1.0 - k_ij[i][j]) * sqrt(ai * aj);
                asummer += molefrac[i] * molefrac[j] * a_ij;
            }
        }
        return std::make_tuple(asummer, bsummer);
    }

    template<typename TType, typename RhoType, typename VecType>
    auto alphar(const TType T, const RhoType rhomolar, const VecType& molefrac) const {
        auto [a_cubic, b_cubic] = get_ab(T, molefrac);
        auto R_gas = 8.3144598;
        return forceeval(-log(1 - b_cubic * rhomolar) - a_cubic / R_gas / T * log((delta_1 * b_cubic * rhomolar + 1) / (delta_2 * b_cubic * rhomolar + 1)) / b_cubic / (delta_1 - delta_2));
    }
};

template<typename Cubic>
class CPAAssociation {
private:
    const Cubic& cubic;
    const std::vector<association_classes> classes;
    const std::vector<int> N_sites;
    const std::valarray<double> epsABi, betaABi;

    auto get_N_sites(const std::vector<association_classes> &classes) {
        std::vector<int> N_sites_out;
        auto get_N = [](auto cl) {
            switch (cl) {
            case association_classes::a1A: return 1;
            case association_classes::a2B: return 2;
            case association_classes::a3B: return 3;
            case association_classes::a4C: return 4;
            default: throw std::invalid_argument("Bad association class");
            }
        };
        for (auto cl : classes) {  
            N_sites_out.push_back(get_N(cl));
        }
        return N_sites_out;
    }

public:
    CPAAssociation(const Cubic &cubic, const std::vector<association_classes>& classes, const std::valarray<double> &epsABi, const std::valarray<double> &betaABi) 
        : cubic(cubic), classes(classes), epsABi(epsABi), betaABi(betaABi), N_sites(get_N_sites(classes)) {};

    template<typename TType, typename RhoType, typename VecType>
    auto alphar(const TType& T, const RhoType& rhomolar, const VecType& molefrac) const {
        // Calculate a and b of the mixture
        auto [a_cubic, b_cubic] = cubic.get_ab(T, molefrac);

        // Calculate the fraction of sites not bonded with other active sites
        auto XA = XA_calc_pure(N_sites[0], classes[0], epsABi[0], betaABi[0], b_cubic, T, rhomolar, molefrac);

        using return_type = std::common_type_t<decltype(T), decltype(rhomolar), decltype(molefrac[0])>;
        return_type alpha_r_asso = 0.0;
        auto i = 0;
        for (auto xi : molefrac){ // loop over all components
            auto XAi = XA.col(i);
            alpha_r_asso += forceeval(xi * (log(XAi) - XAi / 2).sum());
            alpha_r_asso += xi*static_cast<double>(N_sites[i])/2;
            i++;
        }
        return forceeval(alpha_r_asso);
    }
};

template <typename Cubic, typename Assoc>
class CPA {
public:
    Cubic cubic;
    Assoc assoc;

    CPA(Cubic cubic, Assoc assoc) : cubic(cubic), assoc(assoc) {
    }

    /// Residual dimensionless Helmholtz energy from the SRK or PR core and contribution due to association
    /// alphar = a/(R*T) where a and R are both molar quantities
    template<typename TType, typename RhoType, typename VecType>
    auto alphar(const TType& T, const RhoType& rhomolar, const VecType& molefrac) const {

        // Calculate the contribution to alphar from the conventional cubic EOS
        auto alpha_r_cubic = cubic.alphar(T, rhomolar, molefrac);

        // Calculate the contribution to alphar from association
        auto alpha_r_assoc = assoc.alphar(T, rhomolar, molefrac); 

        return forceeval(alpha_r_cubic + alpha_r_assoc);
    }
};