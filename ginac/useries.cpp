/** @file useries.cpp
 *
 *  Functions for extended truncated univariate power series. */

/*
 *  Copyright (C) 2016  Ralf Stephan <ralf@ark.in-berlin.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __PYNAC_USERIES_CPP__
#define __PYNAC_USERIES_CPP__

#include "useries.h"
#include "add.h"
#include "mul.h"
#include "power.h"
#include "symbol.h"
#include "function.h"
#include "relational.h"
#include "inifcns.h"
#include "utils.h"

#include "gmp.h"
#include "flint/fmpq_poly.h"
#include "flint/fmpq.h"

extern "C" void fmpq_get_mpz_frac(mpz_t a, mpz_t b, fmpq_t c);
extern "C" void fmpq_init_set_mpz_frac_readonly(fmpq_t z, const mpz_t p, const mpz_t q);

#include <unordered_map>

namespace GiNaC {

static bool first_symbol = true;
static symbol symb;

static void check_poly_ccoeff_zero(fmpq_poly_t fp)
{
        fmpq_t c;
        fmpq_init(c);
        fmpq_poly_get_coeff_fmpq(c, fp, 0);
        if (not fmpq_is_zero(c))
                throw flint_error();
        fmpq_clear(c);
}

static void check_poly_ccoeff_one(fmpq_poly_t fp)
{
        fmpq_t c;
        fmpq_init(c);
        fmpq_poly_get_coeff_fmpq(c, fp, 0);
        if (not fmpq_is_one(c))
                throw flint_error();
        fmpq_clear(c);
}

static void exp_useries(fmpq_poly_t& fp, fmpq_poly_t& arg, int order)
{
        check_poly_ccoeff_zero(arg);
        fmpq_poly_exp_series(fp, arg, order);
}

static void log_useries(fmpq_poly_t& fp, fmpq_poly_t& arg, int order)
{
        check_poly_ccoeff_one(arg);
        fmpq_poly_log_series(fp, arg, order);
}

static void sin_useries(fmpq_poly_t& fp, fmpq_poly_t& arg, int order)
{
        check_poly_ccoeff_zero(arg);
        fmpq_poly_sin_series(fp, arg, order);
}

static void cos_useries(fmpq_poly_t& fp, fmpq_poly_t& arg, int order)
{
        check_poly_ccoeff_zero(arg);
        fmpq_poly_cos_series(fp, arg, order);
}

static void tan_useries(fmpq_poly_t& fp, fmpq_poly_t& arg, int order)
{
        check_poly_ccoeff_zero(arg);
        fmpq_poly_tan_series(fp, arg, order);
}

static void sinh_useries(fmpq_poly_t& fp, fmpq_poly_t& arg, int order)
{
        check_poly_ccoeff_zero(arg);
        fmpq_poly_sinh_series(fp, arg, order);
}

static void cosh_useries(fmpq_poly_t& fp, fmpq_poly_t& arg, int order)
{
        check_poly_ccoeff_zero(arg);
        fmpq_poly_cosh_series(fp, arg, order);
}

static void tanh_useries(fmpq_poly_t& fp, fmpq_poly_t& arg, int order)
{
        check_poly_ccoeff_zero(arg);
        fmpq_poly_tanh_series(fp, arg, order);
}

static void asin_useries(fmpq_poly_t& fp, fmpq_poly_t& arg, int order)
{
        check_poly_ccoeff_zero(arg);
        fmpq_poly_asin_series(fp, arg, order);
}

static void asinh_useries(fmpq_poly_t& fp, fmpq_poly_t& arg, int order)
{
        check_poly_ccoeff_zero(arg);
        fmpq_poly_asinh_series(fp, arg, order);
}

static void atan_useries(fmpq_poly_t& fp, fmpq_poly_t& arg, int order)
{
        check_poly_ccoeff_zero(arg);
        fmpq_poly_atan_series(fp, arg, order);
}

static void atanh_useries(fmpq_poly_t& fp, fmpq_poly_t& arg, int order)
{
        check_poly_ccoeff_zero(arg);
        fmpq_poly_atanh_series(fp, arg, order);
}

using usfun_t = decltype(exp_useries);
static std::unordered_map<unsigned int,usfun_t*> funcmap {{
        {exp_SERIAL::serial, &exp_useries},
        {log_SERIAL::serial, &log_useries},
        {sin_SERIAL::serial, &sin_useries},
        {cos_SERIAL::serial, &cos_useries},
        {tan_SERIAL::serial, &tan_useries},
        {sinh_SERIAL::serial, &sinh_useries},
        {cosh_SERIAL::serial, &cosh_useries},
        {tanh_SERIAL::serial, &tanh_useries},
        {asin_SERIAL::serial, &asin_useries},
        {asinh_SERIAL::serial, &asinh_useries},
        {atan_SERIAL::serial, &atan_useries},
        {atanh_SERIAL::serial, &atanh_useries},
}};

// Fast heuristic that rejects/accepts expressions for the fast
// expansion via Flint. It can give false positives that must be
// caught before Flint raises SIGABRT, because we want to use the
// older ::series() methods in case. Details:
//
// Does the expression have inexact values, constants, or such?
// It should practically consist of one symbol appearing in
// polynomials from QQ[], and only functions from a supported set.
// The helper uses recurrence to check that all numerics are from QQ,
// that there is not more than one symbol, no constants, and all
// function serial numbers are in the funcmap keys.
static bool unhandled_elements_in(ex the_ex) {

        if (is_exactly_a<constant>(the_ex))
                return true;
        if (is_exactly_a<numeric>(the_ex))
                return not (ex_to<numeric>(the_ex).is_mpz()
                                or ex_to<numeric>(the_ex).is_mpq());
        if (is_exactly_a<symbol>(the_ex)) {
                if (not first_symbol)
                        return (not ex_to<symbol>(the_ex).is_equal(symb));
                first_symbol = false;
                symb = ex_to<symbol>(the_ex);
                return false;
        }
        if (is_exactly_a<function>(the_ex)) {
                function f = ex_to<function>(the_ex);
                if (funcmap.find(f.get_serial()) == funcmap.end())
                        return true;
                for (unsigned int i=0; i<f.nops(); i++)
                        if (unhandled_elements_in(f.op(i)))
                                return true;
                return false;
        }
        if (is_exactly_a<power>(the_ex)) {
                power pow = ex_to<power>(the_ex);
                return (unhandled_elements_in(pow.op(0))
                     or unhandled_elements_in(pow.op(1)));
        }
        if (is_a<expairseq>(the_ex)) {
                const expairseq& epseq = ex_to<expairseq>(the_ex);
                for (unsigned int i=0; i<epseq.nops(); i++) {
                        if (unhandled_elements_in(epseq.op(i)))
                                return true;
                }
                if (unhandled_elements_in(epseq.op(epseq.nops())))
                        return true;
                return false;
        }
        return true;
}

bool useries_can_handle(ex the_ex) {

        return (not unhandled_elements_in(the_ex));
}

ex useries(ex the_ex, const relational & r, int order, unsigned options)
{
        symbol x = ex_to<symbol>(r.lhs());
        fmpq_poly_t fp;
        fmpq_poly_init(fp);
        fmpq_poly_set_ui(fp, 0);
        the_ex.useries(fp, order);
        epvector epv;

        for (slong n=0; n<order; n++) {
                fmpq_t c;
                fmpq_init(c);
                fmpq_poly_get_coeff_fmpq(c, fp, n);
                if (not fmpq_is_zero(c)) {
                        mpq_t gc;
                        mpq_init(gc);
                        fmpq_get_mpq(gc, c);
                        numeric nc(gc); // numeric clears gc
                        epv.push_back(expair(nc, numeric(n)));
                }
                fmpq_clear(c);
        }
        fmpq_poly_clear(fp);
        epv.push_back(expair(Order(_ex1), order));
        return pseries(r, epv);
}

void symbol::useries(fmpq_poly_t& fp, int order) const
{
        fmpq_poly_set_str(fp, "2  0 1");
}

void add::useries(fmpq_poly_t& fp, int order) const
{
        fmpq_poly_set_ui(fp, 0);
        for (const auto & elem : seq) {
		const ex& t = recombine_pair_to_ex(elem);
                fmpq_poly_t fp1;
                fmpq_poly_init(fp1);
                t.useries(fp1, order);
                fmpq_poly_add(fp, fp, fp1);
                fmpq_poly_clear(fp1);
        }
        ex ovcoeff = op(nops());
        if (not is_exactly_a<numeric>(ovcoeff))
                throw std::runtime_error("non-numeric oc encountered");
        numeric oc = ex_to<numeric>(ovcoeff);
        if (oc.is_zero())
                return;

        fmpq_poly_t fp1;
        fmpq_poly_init(fp1);
        if (oc.is_mpz())
                fmpq_poly_set_mpz(fp1, oc.as_mpz());
        else
                fmpq_poly_set_mpq(fp1, oc.as_mpq());
        fmpq_poly_add(fp, fp, fp1);
        fmpq_poly_clear(fp1);
}

void mul::useries(fmpq_poly_t& fp, int order) const
{
        fmpq_poly_set_ui(fp, 1);
        for (const auto & elem : seq) {
		const ex& t = recombine_pair_to_ex(elem);
                fmpq_poly_t fp1;
                fmpq_poly_init(fp1);
                t.useries(fp1, order);
                fmpq_poly_mullow(fp, fp, fp1, order+2);
                fmpq_poly_clear(fp1);
        }
        ex ovcoeff = op(nops());
        if (not is_exactly_a<numeric>(ovcoeff))
                throw std::runtime_error("non-numeric oc encountered");
        numeric oc = ex_to<numeric>(ovcoeff);
        if (oc.is_equal(*_num1_p))
                return;

        if (oc.is_mpz())
                fmpq_poly_scalar_mul_mpz(fp, fp, oc.as_mpz());
        else
                fmpq_poly_scalar_mul_mpq(fp, fp, oc.as_mpq());
}

void power::useries(fmpq_poly_t& fp, int order) const
{
        fmpq_poly_t fp1;
        fmpq_poly_init(fp1);
        basis.useries(fp1, order);
        if (not is_exactly_a<numeric>(exponent)) {
                fmpq_poly_log_series(fp1, fp1, order);
                exponent.useries(fp, order);
                fmpq_poly_mullow(fp, fp, fp1, order+2);
                fmpq_poly_exp_series(fp, fp, order);
                fmpq_poly_clear(fp1);
                return;
        }
        numeric nexp = ex_to<numeric>(exponent);
        if (nexp.is_mpq()) {
                int num = nexp.numer().to_int();
                int den = nexp.denom().to_int();
                if (den == 2) { // exponent of form n/2
                        fmpq_t c;
                        fmpq_init(c);
                        fmpq_poly_get_coeff_fmpq(c, fp1, 0);
                        mpz_t cnum, cden;
                        mpz_init(cnum);
                        mpz_init(cden);
                        fmpq_get_mpz_frac(cnum, cden, c);
                        if (not mpz_perfect_square_p(cnum)
                            or not mpz_perfect_square_p(cden))
                                throw flint_error();
                        mpz_sqrt(cnum, cnum);
                        mpz_sqrt(cden, cden);
                        fmpq_t cc;
                        fmpq_init_set_mpz_frac_readonly(cc, cnum, cden);
                        mpz_clear(cnum);
                        mpz_clear(cden);

                        fmpq_poly_scalar_div_fmpq(fp1, fp1, c);
                        fmpq_poly_sqrt_series(fp1, fp1, order);
                        fmpq_poly_scalar_mul_fmpq(fp1, fp1, cc);
                        if (num > 0)
                                fmpq_poly_pow(fp, fp1, num);
                        else {
                                fmpq_poly_inv_series(fp1, fp1, order);
                                fmpq_poly_pow(fp, fp1, -num);
                        }
                        fmpq_clear(c);
                        fmpq_poly_clear(fp1);
                        return;
                }
                fmpq_poly_log_series(fp1, fp1, order);
                fmpq_poly_scalar_mul_mpq(fp1, fp1, nexp.as_mpq());
                fmpq_poly_exp_series(fp, fp1, order);
                fmpq_poly_clear(fp1);
                return;
        }
        // integer exponent
        int expint = nexp.to_int();
        if (expint > 0) {
                fmpq_poly_pow(fp, fp1, expint);
                fmpq_poly_clear(fp1);
                return;
        }
        else if (expint < 0) {
                check_poly_ccoeff_one(fp1);
                fmpq_poly_inv_series(fp1, fp1, order);
                fmpq_poly_pow(fp, fp1, -expint);
                fmpq_poly_clear(fp1);
                return;
        }
        fmpq_poly_set_str(fp, "1 1");
        fmpq_poly_clear(fp1);
}

void function::useries(fmpq_poly_t& fp, int order) const
{
        auto search = funcmap.find(serial);
        if (search == funcmap.end())
                throw std::runtime_error("can't happen in function::useries");
        fmpq_poly_t fp1;
        fmpq_poly_init(fp1);
        seq[0].useries(fp1, order);
        (*search->second)(fp, fp1, order);
        fmpq_poly_clear(fp1);
}

void numeric::useries(fmpq_poly_t& fp, int order) const
{
        if (is_mpz())
                fmpq_poly_set_mpz(fp, as_mpz());
        else
                fmpq_poly_set_mpq(fp, as_mpq());
}

} // namespace GiNaC

#endif // undef __PYNAC_USERIES_CPP__