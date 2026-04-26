#pragma once

#include "types.h"

namespace osuCrypto
{
    inline double RBDefaultEps(size_t n, size_t lambda = 40)
    {
        (void)lambda;
        n = std::max<size_t>(n, 1);

        if (n <= 128)
            return 0.16;
        if (n <= 256)
            return 0.07;
        if (n <= 512)
            return 0.10;
        return 0.07;
    }

    inline size_t RBRoundW(size_t w)
    {
        w = std::max<size_t>(w, 16);
        const size_t step = 16;
        w = ((w + step - 1) / step) * step;
        if (w > 256)
            w = 256;
        return w;
    }

    inline double RBLog2(size_t n)
    {
        return std::log2(static_cast<double>(std::max<size_t>(n, 1)));
    }

    inline double RBLerp(double a, double b, double t)
    {
        return a + (b - a) * t;
    }

    inline bool RBApprox(double a, double b, double tol = 0.005)
    {
        return std::abs(a - b) <= tol;
    }

    inline double RBInterp(const std::vector<double>& xs, const std::vector<double>& ys, double x)
    {
        if (xs.empty() || ys.empty() || xs.size() != ys.size())
        {
            throw std::invalid_argument("RBInterp bad table");
        }

        if (x <= xs.front())
        {
            return ys.front();
        }
        if (x >= xs.back())
        {
            const size_t i = xs.size() - 2;
            const double t = (x - xs[i]) / (xs[i + 1] - xs[i]);
            return RBLerp(ys[i], ys[i + 1], t);
        }

        for (size_t i = 0; i + 1 < xs.size(); ++i)
        {
            if (x <= xs[i + 1])
            {
                const double t = (x - xs[i]) / (xs[i + 1] - xs[i]);
                return RBLerp(ys[i], ys[i + 1], t);
            }
        }

        return ys.back();
    }

    inline double RBGOf(size_t n, double eps)
    {
        const double eps2 = std::round(eps * 100.0) / 100.0;

        // empirical fit for 2^7 to 2^9
        if (n <= 128)
        {
            if (RBApprox(eps2, 0.14)) return 21.51;
            if (RBApprox(eps2, 0.15)) return 26.80;
            if (RBApprox(eps2, 0.16)) return 25.91;
            if (RBApprox(eps2, 0.17)) return 21.29;
            if (RBApprox(eps2, 0.18)) return 24.15;
            if (RBApprox(eps2, 0.19)) return 23.27;
            if (RBApprox(eps2, 0.20)) return 22.39;
        }
        if (n <= 256)
        {
            if (RBApprox(eps2, 0.07)) return 27.68;
            if (RBApprox(eps2, 0.08)) return 27.68;
            if (RBApprox(eps2, 0.09)) return 24.15;
            if (RBApprox(eps2, 0.10)) return 26.80;
            if (RBApprox(eps2, 0.11)) return 27.90;
            if (RBApprox(eps2, 0.12)) return 26.80;
        }
        if (n <= 512)
        {
            if (RBApprox(eps2, 0.07)) return 30.76;
            if (RBApprox(eps2, 0.08)) return 27.68;
            if (RBApprox(eps2, 0.09)) return 28.12;
            if (RBApprox(eps2, 0.10)) return 26.80;
            if (RBApprox(eps2, 0.11)) return 27.90;
            if (RBApprox(eps2, 0.12)) return 26.80;
        }

        // paper fit
        static const std::vector<double> epsXs = {0.03, 0.05, 0.07, 0.10};
        static const std::vector<double> logXs = {10.0, 14.0, 16.0, 18.0, 20.0};
        static const std::vector<std::vector<double>> gGrid = {
            {-3.4, -6.1, -7.5, -9.0, -10.7},
            {-4.3, -7.5, -9.2, -10.8, -12.9},
            {-5.3, -8.3, -10.3, -12.1, -14.0},
            {-6.3, -9.4, -11.6, -13.5, -15.3},
        };

        const double logN = std::max(10.0, RBLog2(n));
        std::vector<double> gAtN;
        gAtN.reserve(gGrid.size());
        for (const auto& row : gGrid)
        {
            gAtN.push_back(RBInterp(logXs, row, logN));
        }

        if (eps <= epsXs.front())
        {
            return gAtN.front();
        }
        if (eps >= epsXs.back())
        {
            const size_t i = epsXs.size() - 2;
            const double t = (eps - epsXs[i]) / (epsXs[i + 1] - epsXs[i]);
            return RBLerp(gAtN[i], gAtN[i + 1], t);
        }

        return RBInterp(epsXs, gAtN, eps);
    }

    inline double RBEpsOf(size_t n, size_t columns)
    {
        n = std::max<size_t>(n, 1);
        columns = std::max(columns, n);
        return (static_cast<double>(columns) / static_cast<double>(n)) - 1.0;
    }

    inline double RBLambdaMain(size_t bandWidth, double eps)
    {
        if (eps <= 0.0 || bandWidth == 0)
        {
            return 0.0;
        }
        return 2.751 * eps * static_cast<double>(bandWidth);
    }

    inline double RBLambdaOf(size_t n, size_t bandWidth, double eps)
    {
        return RBLambdaMain(bandWidth, eps) + RBGOf(n, eps);
    }

    inline size_t RBNeedW(size_t n, size_t lambda, double eps)
    {
        if (eps <= 0.0)
        {
            throw std::invalid_argument("RBNeedW eps <= 0");
        }

        const double g = RBGOf(n, eps);
        const double needD = std::ceil((static_cast<double>(lambda) - g) / (2.751 * eps));
        if (needD <= 1.0)
        {
            return 1;
        }
        return static_cast<size_t>(needD);
    }

    inline size_t RBWidth(size_t n, size_t lambda = 40)
    {
        n = std::max<size_t>(n, 1);

        if (n <= 128)
            return 64;
        if (n <= 256)
            return 144;
        if (n <= 512)
            return 96;
        if (n <= 2048)
            return 240;

        const double eps = RBDefaultEps(n, lambda);
        const double g = RBGOf(n, eps);
        const double needD = std::max<double>(
            1.0,
            std::ceil((static_cast<double>(lambda) - g) / (2.751 * eps)));
        return RBRoundW(static_cast<size_t>(needD));
    }

    inline size_t RBSize(size_t n, size_t lambda = 40)
    {
        n = std::max<size_t>(n, 1);
        const double eps = RBDefaultEps(n, lambda);
        return std::max<size_t>(n, static_cast<size_t>(std::ceil((1.0 + eps) * static_cast<double>(n))));
    }

    inline size_t RBTableSize(size_t n, RBParams params = {})
    {
        n = std::max<size_t>(n, 1);

        if (params.columns != 0)
        {
            return std::max(params.columns, n);
        }
        if (params.eps > 0.0)
        {
            return std::max<size_t>(
                n,
                static_cast<size_t>(std::ceil((1.0 + params.eps) * static_cast<double>(n))));
        }

        return RBSize(n, params.lambda);
    }

    inline size_t RBTableW(size_t n, size_t columns, RBParams params = {})
    {
        if (params.bandWidth != 0)
        {
            return params.bandWidth;
        }
        if (params.columns != 0 || params.eps > 0.0)
        {
            return RBNeedW(n, params.lambda, RBEpsOf(n, columns));
        }
        return RBWidth(n, params.lambda);
    }

    inline void RBCheck(size_t n, RBParams params, const RBInfo& info)
    {
        if (params.lambda == 0)
        {
            throw std::invalid_argument("RBCheck lambda == 0");
        }
        if (params.eps < 0.0)
        {
            throw std::invalid_argument("RBCheck eps < 0");
        }
        if (info.bandWidth > 256)
        {
            throw std::invalid_argument("RBCheck width > 256");
        }
        if (info.bandWidth > info.columns)
        {
            throw std::invalid_argument("RBCheck width > columns");
        }
        if (params.columns != 0 && params.eps > 0.0)
        {
            const size_t wantCols = std::max<size_t>(
                std::max<size_t>(n, 1),
                static_cast<size_t>(std::ceil((1.0 + params.eps) * static_cast<double>(std::max<size_t>(n, 1)))));
            if (params.columns != wantCols)
            {
                throw std::invalid_argument("RBCheck eps and columns mismatch");
            }
        }

        const size_t needW = RBNeedW(info.n, info.lambda, info.eps);
        if (info.bandWidth < needW)
        {
            throw std::invalid_argument(
                "RBCheck lambda target not met n=" + std::to_string(info.n) +
                " lambda=" + std::to_string(info.lambda) +
                " eps=" + std::to_string(info.eps) +
                " w=" + std::to_string(info.bandWidth) +
                " needW=" + std::to_string(needW) +
                " lambdaReal=" + std::to_string(info.lambdaReal));
        }
    }

    inline RBInfo RBInfoOf(size_t n, RBParams params = {})
    {
        RBInfo out{};
        out.n = std::max<size_t>(n, 1);
        out.lambda = params.lambda;
        out.columns = RBTableSize(out.n, params);
        out.eps = RBEpsOf(out.n, out.columns);
        out.bandWidth = RBTableW(out.n, out.columns, params);
        out.lambdaReal = RBLambdaOf(out.n, out.bandWidth, out.eps);

        if (out.bandWidth == 0)
        {
            out.bandWidth = 1;
        }
        if (out.bandWidth > 256)
        {
            throw std::invalid_argument("RBInfoOf width > 256");
        }
        if (out.bandWidth > out.columns)
        {
            throw std::invalid_argument("RBInfoOf width > columns");
        }

        RBCheck(out.n, params, out);
        return out;
    }
}
