#pragma once

#include "json.hpp"
#include <Eigen/Dense>
#include <fstream>
#include <string>

template<typename EOSCollection>
class CorrespondingStatesContribution {

private:
    const EOSCollection EOSs;
public:
    CorrespondingStatesContribution(EOSCollection&& EOSs) : EOSs(EOSs) {};

    template<typename TauType, typename DeltaType, typename MoleFractions>
    auto alphar(const TauType& tau, const DeltaType& delta, const MoleFractions& molefracs) const {
        using resulttype = decltype(tau* delta* molefracs[0]);
        resulttype alphar = 0.0;
        auto N = molefracs.size();
        for (auto i = 0; i < N; ++i) {
            alphar = alphar + molefracs[i] * EOSs[i].alphar(tau, delta);
        }
        return alphar;
    }
};

template<typename FCollection, typename DepartureFunctionCollection>
class DepartureContribution {

private:
    const FCollection F;
    const DepartureFunctionCollection funcs;
public:
    DepartureContribution(FCollection&& F, DepartureFunctionCollection&& funcs) : F(F), funcs(funcs) {};

    template<typename TauType, typename DeltaType, typename MoleFractions>
    auto alphar(const TauType& tau, const DeltaType& delta, const MoleFractions& molefracs) const {
        using resulttype = decltype(tau* delta* molefracs[0]);
        resulttype alphar = 0.0;
        auto N = molefracs.size();
        for (auto i = 0; i < N; ++i) {
            for (auto j = i+1; j < N; ++j) {
                alphar = alphar + molefracs[i] * molefracs[j] * F(i,j) * funcs[i][j].alphar(tau, delta);
            }
        }
        return alphar;
    }
};

template<typename ReducingFunction, typename CorrespondingTerm, typename DepartureTerm>
class MultiFluid {  

public:
    const ReducingFunction redfunc;
    const CorrespondingTerm corr;
    const DepartureTerm dep;

    const double R = get_R_gas<double>();

    MultiFluid(ReducingFunction&& redfunc, CorrespondingTerm&& corr, DepartureTerm&& dep) : redfunc(redfunc), corr(corr), dep(dep) {};

    template<typename TType, typename RhoType>
    auto alphar(TType T,
        const RhoType& rhovec,
        const std::optional<typename RhoType::value_type> rhotot = std::nullopt) const
    {
        RhoType::value_type rhotot_ = (rhotot.has_value()) ? rhotot.value() : std::accumulate(std::begin(rhovec), std::end(rhovec), (decltype(rhovec[0]))0.0);
        auto molefrac = rhovec / rhotot_;
        auto Tred = redfunc.get_Tr(molefrac);
        auto rhored = redfunc.get_rhor(molefrac);
        auto delta = rhotot_ / rhored;
        auto tau = Tred / T;
        using resulttype = decltype(T* rhovec[0]);
        return corr.alphar(tau, delta, molefrac) + dep.alphar(tau, delta, molefrac);
    }
};


class MultiFluidReducingFunction {
private:
    Eigen::MatrixXd betaT, gammaT, betaV, gammaV, YT, Yv;
    

    template <typename Num>
    auto cube(Num x) const {
        return x*x*x;
    }
    template <typename Num>
    auto square(Num x) const {
        return x*x;
    }

public:
    Eigen::ArrayXd Tc, vc;

    template<typename ArrayLike>
    MultiFluidReducingFunction(
        const Eigen::MatrixXd& betaT, const Eigen::MatrixXd& gammaT,
        const Eigen::MatrixXd& betaV, const Eigen::MatrixXd& gammaV,
        const ArrayLike& Tc, const ArrayLike& vc)
        : betaT(betaT), gammaT(gammaT), betaV(betaV), gammaV(gammaV), Tc(Tc), vc(vc) {

        auto N = Tc.size();

        YT.resize(N, N); YT.setZero();
        Yv.resize(N, N); Yv.setZero();
        for (auto i = 0; i < N; ++i) {
            for (auto j = i + 1; j < N; ++j) {
                YT(i, j) = betaT(i, j) * gammaT(i, j) * sqrt(Tc[i] * Tc[j]);
                YT(j, i) = betaT(j, i) * gammaT(j, i) * sqrt(Tc[i] * Tc[j]);
                Yv(i, j) = 1.0 / 8.0 * betaV(i, j) * gammaV(i, j) * cube(cbrt(vc[i]) + cbrt(vc[j]));
                Yv(j, i) = 1.0 / 8.0 * betaV(j, i) * gammaV(j, i) * cube(cbrt(vc[i]) + cbrt(vc[j]));
            }
        }
    }

    template <typename MoleFractions>
    auto Y(const MoleFractions& z, const Eigen::ArrayXd& Yc, const Eigen::MatrixXd& beta, const Eigen::MatrixXd& Yij) const {

        auto N = z.size();
        MoleFractions::value_type sum1 = 0.0;
        for (auto i = 0; i < N; ++i) {
            sum1 = sum1 + square(z[i]) * Yc[i];
        }
        
        MoleFractions::value_type sum2 = 0.0;
        for (auto i = 0; i < N-1; ++i){
            for (auto j = i+1; j < N; ++j) {
                sum2 = sum2 + 2*z[i]*z[j]*(z[i] + z[j])/(square(beta(i, j))*z[i] + z[j])*Yij(i, j);
            }
        }

        return sum1 + sum2;
    }

    static auto get_BIPdep(const nlohmann::json& collection, const std::vector<std::string>& components) {

        // convert string to upper case
        auto toupper = [](const std::string s){ auto data = s; std::for_each(data.begin(), data.end(), [](char& c) { c = ::toupper(c); }); return data;};

        std::string comp0 = toupper(components[0]);
        std::string comp1 = toupper(components[1]);
        for (auto& el : collection) {
            std::string name1 = toupper(el["Name1"]);
            std::string name2 = toupper(el["Name2"]);
            if (comp0 == name1 && comp1 == name2) {
                return el;
            }
            if (comp0 == name2 && comp1 == name1) {
                return el;
            }
        }
        throw std::invalid_argument("Can't match this binary pair");
    }
    static auto get_binary_interaction_double(const nlohmann::json& collection, const std::vector<std::string>& components) {
        auto el = get_BIPdep(collection, components);

        double betaT = el["betaT"], gammaT = el["gammaT"], betaV = el["betaV"], gammaV = el["gammaV"];
        // Backwards order of components, flip beta values
        if (components[0] == el["Name2"] && components[1] == el["Name1"]) {
            betaT = 1.0 / betaT;
            betaV = 1.0 / betaV;
        }
        return std::make_tuple(betaT, gammaT, betaV, gammaV);
    }
    static auto get_BIP_matrices(const nlohmann::json& collection, const std::vector<std::string>& components) {
        Eigen::MatrixXd betaT, gammaT, betaV, gammaV, YT, Yv;
        auto N = components.size();
        betaT.resize(N, N); betaT.setZero();
        gammaT.resize(N, N); gammaT.setZero();
        betaV.resize(N, N); betaV.setZero();
        gammaV.resize(N, N); gammaV.setZero();
        for (auto i = 0; i < N; ++i) {
            for (auto j = i + 1; j < N; ++j) {
                auto [betaT_, gammaT_, betaV_, gammaV_] = get_binary_interaction_double(collection, { components[i], components[j] });
                betaT(i, j) = betaT_;         betaT(j, i) = 1.0 / betaT(i, j);
                gammaT(i, j) = gammaT_;       gammaT(j, i) = gammaT(i, j);
                betaV(i, j) = betaV_;         betaV(j, i) = 1.0 / betaV(i, j);
                gammaV(i, j) = gammaV_;       gammaV(j, i) = gammaV(i, j);
            }
        }
        return std::make_tuple(betaT, gammaT, betaV, gammaV);
    }
    static auto get_Tcvc(const std::string& coolprop_root, const std::vector<std::string>& components) {
        Eigen::ArrayXd Tc(components.size()), vc(components.size());
        using namespace nlohmann;
        auto i = 0;
        for (auto& c : components) {
            auto j = json::parse(std::ifstream(coolprop_root + "/dev/fluids/" + c + ".json"));
            auto red = j["EOS"][0]["STATES"]["reducing"];
            double Tc_ = red["T"];
            double rhoc_ = red["rhomolar"];
            Tc[i] = Tc_;
            vc[i] = 1.0 / rhoc_;
            i++;
        }
        return std::make_tuple(Tc, vc);
    }
    static auto get_F_matrix(const nlohmann::json& collection, const std::vector<std::string>& components) {
        Eigen::MatrixXd F(components.size(), components.size());
        auto N = components.size();
        for (auto i = 0; i < N; ++i) {
            F(i, i) = 0.0;
            for (auto j = i + 1; j < N; ++j) {
                auto el = get_BIPdep(collection, { components[i], components[j] });
                if (el.empty()) {
                    F(i, j) = 0.0;
                    F(j, i) = 0.0;
                }
                else{
                    F(i, j) = el["F"];
                    F(j, i) = el["F"];
                }   
            }
        }
        return F;
    }
    template<typename MoleFractions> auto get_Tr(const MoleFractions& molefracs) const { return Y(molefracs, Tc, betaT, YT); }
    template<typename MoleFractions> auto get_rhor(const MoleFractions& molefracs) const { return 1.0 / Y(molefracs, vc, betaV, Yv); }
};

class MultiFluidDepartureFunction {
public:
    enum class types { NOTSETTYPE, GERG2004, GaussianExponential };
private:
    types type = types::NOTSETTYPE;
public:
    Eigen::ArrayXd n, t, d, c, l, eta, beta, gamma, epsilon;

    void set_type(const std::string& kind) {
        if (kind == "GERG-2004" || kind == "GERG-2008") {
            type = types::GERG2004;
        }
        else if (kind == "Gaussian+Exponential") {
            type = types::GaussianExponential;
        }
        else {
            throw std::invalid_argument("Bad type:" + kind);
        }
    }

    template<typename TauType, typename DeltaType>
    auto alphar(const TauType& tau, const DeltaType& delta) const {
        switch (type) {
        case (types::GaussianExponential):
            return (n * pow(tau, t) * pow(delta, d) * exp(-c * pow(delta, l)) * exp(-eta * (delta - epsilon).square() - beta * (tau - gamma).square())).sum();
        case (types::GERG2004):
            return (n * pow(tau, t) * pow(delta, d) * exp(-eta * (delta - epsilon).square() - beta * (delta - gamma))).sum();
        default:
            throw - 1;
        }
    }
};

auto get_departure_function_matrix(const std::string& coolprop_root, const nlohmann::json& BIPcollection, const std::vector<std::string>& components) {

    // Allocate the matrix with default models
    std::vector<std::vector<MultiFluidDepartureFunction>> funcs(2); for (auto i = 0; i < funcs.size(); ++i) { funcs[i].resize(funcs.size()); }

    auto depcollection = nlohmann::json::parse(std::ifstream(coolprop_root + "/dev/mixtures/mixture_departure_functions.json"));

    auto get_departure_function = [&depcollection](const std::string& Name) {
        for (auto& el : depcollection) {
            if (el["Name"] == Name) { return el; }
        }
        throw std::invalid_argument("Bad argument");
    };

    for (auto i = 0; i < funcs.size(); ++i) {
        for (auto j = i + 1; j < funcs.size(); ++j) {
            auto BIP = MultiFluidReducingFunction::get_BIPdep(BIPcollection, { components[i], components[j] });
            auto function = BIP["function"];
            if (!function.empty()) {

                auto info = get_departure_function(function);
                auto N = info["n"].size();

                auto toeig = [](const std::vector<double>& v) -> Eigen::ArrayXd { return Eigen::Map<const Eigen::ArrayXd>(&(v[0]), v.size()); };
                auto eigorempty = [&info, &toeig, &N](const std::string& name) -> Eigen::ArrayXd {
                    if (!info[name].empty()) {
                        return toeig(info[name]);
                    }
                    else {
                        return Eigen::ArrayXd::Zero(N);
                    }
                };

                MultiFluidDepartureFunction f;
                f.set_type(info["type"]);
                f.n = toeig(info["n"]);
                f.t = toeig(info["t"]);
                f.d = toeig(info["d"]);

                f.eta = eigorempty("eta");
                f.beta = eigorempty("beta");
                f.gamma = eigorempty("gamma");
                f.epsilon = eigorempty("epsilon");

                Eigen::ArrayXd c(f.n.size()), l(f.n.size()); c.setZero();
                if (info["l"].empty()) {
                    // exponential part not included
                    l.setZero();
                }
                else {
                    l = toeig(info["l"]);
                    // l is included, use it to build c; c_i = 1 if l_i > 0, zero otherwise
                    for (auto i = 0; i < c.size(); ++i) {
                        if (l[i] > 0) {
                            c[i] = 1.0;
                        }
                    }
                }

                f.l = l;
                f.c = c;
                funcs[i][j] = f;
                funcs[j][i] = f;
                int rr = 0;
            }
        }
    }
    return funcs;
}

class MultiFluidEOS {
public:
    enum class types { NOTSETTYPE, GERG2004, GaussianExponential };
private:
    types type = types::NOTSETTYPE;
public:
    Eigen::ArrayXd n, t, d, c, l, eta, beta, gamma, epsilon;

    void allocate(std::size_t N) {
        auto go = [&N](Eigen::ArrayXd &v){ v.resize(N); v.setZero(); };
        go(n); go(t); go(d); go(l); go(c); go(eta); go(beta); go(gamma); go(epsilon);
    }

    //void set_type(const std::string& kind) {
    //    if (kind == "GERG-2004" || kind == "GERG-2008") {
    //        type = types::GERG2004;
    //    }
    //    else if (kind == "Gaussian+Exponential") {
    //        type = types::GaussianExponential;
    //    }
    //    else {
    //        throw std::invalid_argument("Bad type:" + kind);
    //    }
    //}

    template<typename TauType, typename DeltaType>
    auto alphar(const TauType& tau, const DeltaType& delta) const {
        return (n * pow(tau, t) * pow(delta, d) * exp(-c * pow(delta, l)) * exp(-eta * (delta - epsilon).square() - beta * (tau - gamma).square())).sum();
    }
};

auto get_EOS(const std::string& coolprop_root, const std::string& name) 
{
    using namespace nlohmann;
    auto j = json::parse(std::ifstream(coolprop_root + "/dev/fluids/" + name + ".json"));
    auto alphar = j["EOS"][0]["alphar"];

    std::size_t ncoeff = 0;

    const std::vector<std::string> allowable_types = {"ResidualHelmholtzPower", "ResidualHelmholtzGaussian"};
    auto isallowed = [&allowable_types](const std::string &name){ for (auto &a : allowable_types){ if (name == a){return true;};} return false;};
    for (auto& term : alphar) {
        std::string type = term["type"];
        if (!isallowed(type)){
            throw std::invalid_argument("Bad type:" + type);
        }
        else{
            ncoeff += term["n"].size();
        }
    }
    
    MultiFluidEOS eos; 
    eos.allocate(ncoeff); // Allocate arrays to the right size, fill with zero
    
    auto toeig = [](const std::vector<double>& v) -> Eigen::ArrayXd { return Eigen::Map<const Eigen::ArrayXd>(&(v[0]), v.size()); };
    
    std::size_t offset = 0;
    for (auto &term: alphar){
        std::size_t N = term["n"].size();

        auto eigorzero = [&term, &toeig, &N](const std::string& name) -> Eigen::ArrayXd {
            if (!term[name].empty()) {
                return toeig(term[name]);
            }
            else {
                return Eigen::ArrayXd::Zero(N);
            }
        };        

        eos.n.segment(offset, N) = eigorzero("n");
        eos.t.segment(offset, N) = eigorzero("t");
        eos.d.segment(offset, N) = eigorzero("d");
        eos.eta.segment(offset, N) = eigorzero("eta");
        eos.beta.segment(offset, N) = eigorzero("beta");
        eos.gamma.segment(offset, N) = eigorzero("gamma");
        eos.epsilon.segment(offset, N) = eigorzero("epsilon");

        Eigen::ArrayXd c(N), l(N); c.setZero();
        if (term["l"].empty()) {
            // exponential part not included
            l.setZero();
        }
        else {
            l = toeig(term["l"]);
            // l is included, use it to build c; c_i = 1 if l_i > 0, zero otherwise
            for (auto i = 0; i < c.size(); ++i) {
                if (l[i] > 0) {
                    c[i] = 1.0;
                }
            }
        }
        eos.c.segment(offset, N) = c;
        eos.l.segment(offset, N) = l;

        offset += N;
    }
    return eos;
}

auto get_EOSs(const std::string& coolprop_root, const std::vector<std::string>& names) {
    std::vector<MultiFluidEOS> EOSs;
    for (auto& name : names) {
        EOSs.emplace_back(get_EOS(coolprop_root, name));
    }
    return EOSs;
}

class DummyEOS {
public:
    template<typename TType, typename RhoType> auto alphar(TType tau, const RhoType& delta) const { return tau * delta; }
};
class DummyReducingFunction {
public:
    template<typename MoleFractions> auto get_Tr(const MoleFractions& molefracs) const { return molefracs[0]; }
    template<typename MoleFractions> auto get_rhor(const MoleFractions& molefracs) const { return molefracs[0]; }
};
auto build_dummy_multifluid_model(const std::vector<std::string>& components) {
    std::vector<DummyEOS> EOSs(2);
    std::vector<std::vector<DummyEOS>> funcs(2); for (auto i = 0; i < funcs.size(); ++i) { funcs[i].resize(funcs.size()); }
    std::vector<std::vector<double>> F(2); for (auto i = 0; i < F.size(); ++i) { F[i].resize(F.size()); }

    struct Fwrapper {
    private: 
        const std::vector<std::vector<double>> F_;
    public:
        Fwrapper(const std::vector<std::vector<double>> &F) : F_(F){};
        auto operator ()(std::size_t i, std::size_t j) const{ return F_[i][j]; }
    };
    auto ff = Fwrapper(F);
    auto redfunc = DummyReducingFunction();
    return MultiFluid(std::move(redfunc), std::move(CorrespondingStatesContribution(std::move(EOSs))), std::move(DepartureContribution(std::move(ff), std::move(funcs))));
}
void test_dummy() {
    auto model = build_dummy_multifluid_model({ "A", "B" });
    std::valarray<double> rhovec = { 1.0, 2.0 };
    auto alphar = model.alphar(300.0, rhovec);
}