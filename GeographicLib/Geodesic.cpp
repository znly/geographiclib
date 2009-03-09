/**
 * \file Geodesic.cpp
 * \brief Implementation for GeographicLib::Geodesic class
 *
 * Copyright (c) Charles Karney (2008, 2009) <charles@karney.com>
 * and licensed under the LGPL.  For more information, see
 * http://charles.karney.info/geographic/
 *
 * This is a reformulation of the geodesic problem.  The notation is as
 * follows:
 * - at a general point (no suffix or 1 or 2 as suffix)
 *   - phi = latitude
 *   - beta = latitude on auxilliary sphere
 *   - lambda = longitude on auxilliary sphere
 *   - chi = longitude
 *   - alpha = azimuth of great circle
 *   - sigma = arc length along greate circle
 *   - s = distance
 *   - tau = scaled distance (= sigma at multiples of pi/2)
 * - at previous northwards equator crossing
 *   - beta = phi = 0
 *   - lambda = chi = 0
 *   - alpha = alpha0
 *   - sigma = s = 0
 * - a 12 suffix means a difference, e.g., s12 = s2 - s1.
 * - s and c prefixes mean sin and cos
 **********************************************************************/

#define DEBUG 1
#include "GeographicLib/Geodesic.hpp"
#include "GeographicLib/Constants.hpp"
#include <algorithm>
#include <limits>
#if DEBUG
#include <iostream>
#include <iomanip>
#endif

namespace {
  char RCSID[] = "$Id$";
  char RCSID_H[] = GEODESIC_HPP;
}

namespace GeographicLib {

  using namespace std;

  // Underflow guard.  We require
  //   eps2 * epsilon() > 0
  //   eps2 + epsilon() == epsilon()
  const double Geodesic::eps2 = sqrt(numeric_limits<double>::min());
  const double Geodesic::tol = 100 * numeric_limits<double>::epsilon();
  const double Geodesic::tol1 = sqrt(numeric_limits<double>::epsilon());
  const double Geodesic::xthresh =  10 * tol1;

  Geodesic::Geodesic(double a, double r) throw()
    : _a(a)
    , _f(r > 0 ? 1 / r : 0)
    , _f1(1 - _f)
    , _e2(_f * (2 - _f))
    , _ep2(_e2 / sq(_f1))	// e2 / (1 - e2)
    , _b(_a * _f1)
  {}

  const Geodesic Geodesic::WGS84(Constants::WGS84_a(),
				 Constants::WGS84_r());

  double Geodesic::SinSeries(double sinx, double cosx,
			     const double c[], int n) throw() {
    // Evaluate y = sum(c[i - 1] * sin(2 * i * x), i, 1, n) using Clenshaw
    // summation.  (Indices into c offset by 1.)  THIS REQUIRES THAT n BE
    // POSITIVE.
    double
      ar = 2 * (sq(cosx) - sq(sinx)), // 2 * cos(2 * x)
      y0 = c[n - 1], y1 = 0;	      // Accumulators for sum
    for (int j = n; --j;) {	      // j = n-1 .. 1
      double y2 = y1;
      y1 = y0; y0  = ar * y1 - y2 + c[j - 1];
    }
    return 2 * sinx * cosx * y0; // sin(2 * x) * y0
  }

  GeodesicLine Geodesic::Line(double lat1, double lon1, double azi1)
    const throw() {
    return GeodesicLine(*this, lat1, lon1, azi1);
  }

  void Geodesic::Direct(double lat1, double lon1, double azi1, double s12,
			double& lat2, double& lon2, double& azi2)
    const throw() {
    GeodesicLine l(*this, lat1, lon1, azi1);
    l.Position(s12, lat2, lon2, azi2);
  }

  void Geodesic::Inverse(double lat1, double lon1, double lat2, double lon2,
			 double& s12, double& azi1, double& azi2)
    const throw() {
#if ITER
    iter = 0; iterx = 0;
    // cerr << setprecision(12);
#endif
    lon1 = AngNormalize(lon1);
    double lon12 = AngNormalize(AngNormalize(lon2) - lon1);
    // If very close to being on the same meridian, then make it so.
    // Not sure this is necessary...
    lon12 = AngRound(lon12);
    // Make longitude difference positive.
    int lonsign = lon12 >= 0 ? 1 : -1;
    lon12 *= lonsign;
    // If really close to the equator, treat as on equator.
    lat1 = AngRound(lat1);
    lat2 = AngRound(lat2);
    // Swap points so that point with higher (abs) latitude is point 1
    int swapp = abs(lat1) >= abs(lat2) ? 1 : -1;
    if (swapp < 0) {
      lonsign *= -1;
      swap(lat1, lat2);
    }
    // Make lat1 <= 0
    int latsign = lat1 < 0 ? 1 : -1;
    lat1 *= latsign;
    lat2 *= latsign;
    // Now we have
    //
    //     0 <= lon12 <= 180
    //     -90 <= lat1 <= 0
    //     lat1 <= lat2 <= -lat1
    //
    // longsign, swapp, latsign register the transformation to bring the
    // coordinates to this canonical form.  In all cases, 1 means no change was
    // made.  We make these transformations so that there are few cases to
    // check, e.g., on verifying quadrants in atan2.  In addition, this
    // enforces some symmetries in the results returned.

    double phi, sbet1, cbet1, sbet2, cbet2, n1;

    phi = lat1 * Constants::degree();
    // Ensure cbet1 = +eps at poles
    sbet1 = _f1 * sin(phi);
    cbet1 = lat1 == -90 ? eps2 : cos(phi);
    // n = sqrt(1 - e2 * sq(sin(phi)))
    n1 = hypot(sbet1, cbet1);
    sbet1 /= n1; cbet1 /= n1;

    phi = lat2 * Constants::degree();
    // Ensure cbet2 = +eps at poles
    sbet2 = _f1 * sin(phi);
    cbet2 = abs(lat2) == 90 ? eps2 : cos(phi);
    SinCosNorm(sbet2, cbet2);

    double
      // How close to antipodal lat?
      sbet12 = sbet2 * cbet1 - cbet2 * sbet1, // bet2 - bet1 in [0, pi)
      // cbet12 = cbet2 * cbet1 + sbet2 * sbet1,
      sbet12a = sbet2 * cbet1 + cbet2 * sbet1, // bet2 + bet1 (-pi, 0]
      //      cbet12a = cbet2 * cbet1 - sbet2 * sbet1,
      chi12 = lon12 * Constants::degree(),
      cchi12 = cos(chi12),	// lon12 == 90 isn't interesting
      schi12 = lon12 == 180 ? 0 :sin(chi12);

    double calp1, salp1, calp2, salp2,
      c[maxpow_taucoef > maxpow_lamcoef ? maxpow_taucoef : maxpow_lamcoef];
    // Enumerate all the cases where the geodesic is a meridian.  This includes
    // coincident points.
    if (schi12 == 0 || lat1 == -90) {
      // Head to the target longitude
      calp1 = cchi12; salp1 = schi12;
      // At the target we're heading north
      calp2 = 1; salp2 = 0;

      double
	// tan(bet) = tan(sig) * cos(alp),
	ssig1 = sbet1, csig1 = calp1 * cbet1,
	ssig2 = sbet2, csig2 = calp2 * cbet2;
      SinCosNorm(ssig1, csig1);
      SinCosNorm(ssig2, csig2);
	
      // sig12 = sig2 - sig1
      double sig12 = atan2(max(csig1 * ssig2 - ssig1 * csig2, 0.0),
			   csig1 * csig2 + ssig1 * ssig2);

      tauCoeff(_ep2, c);
      s12 = _b * tauScale(_ep2) *
	(sig12 + (SinSeries(ssig2, csig2, c, maxpow_taucoef) -
		  SinSeries(ssig1, csig1, c, maxpow_taucoef)));
    } else if (sbet1 == 0 &&	// and sbet2 == 0
	       // Mimic the way Chi12 works with calp1 = 0
	       chi12 <= Constants::pi() - _f * Constants::pi()) {
      // Geodesic runs along equator
      calp1 = calp2 = 0; salp1 = salp2 = 1;
      s12 = _a * chi12;
    } else {

      // Now point1 and point2 belong within a hemisphere bounded by a line of
      // longitude (lon = lon12/2 +/- 90).

      {
	// Figure a starting point for Newton's method
	double csig12, ssig12, chicrita;
	csig12 = sbet1 * sbet2 + cbet1 * cbet2 * cchi12;
	salp1 = cbet2 * schi12;
	calp1 = cchi12 >= 0 ?
	  sbet12 * _f1/n1 + cbet2 * sbet1 * sq(schi12) / (1 + cchi12) :
	  sbet12a - cbet2 * sbet1 * sq(schi12) / (1 - cchi12);
	ssig12 = hypot(salp1, calp1);
	chicrita = -cbet1 * dlamScale(_f, sq(sbet1)) * Constants::pi();

	if (csig12 >= 0 || ssig12 >= 3 * chicrita * cbet1)
	  // Zeroth order spherical approximation is OK
	  SinCosNorm(salp1, calp1);
	else {
	  double
	    x = (chi12 - Constants::pi())/chicrita,
	    y = sbet12a / (chicrita * cbet1);
	  if (y > -tol && x >  -1 - xthresh) {
	    // strip near cut
	    salp1 = min(1.0, -x);
	    calp1 = - sqrt(1 - sq(salp1));
	  } else {
	    // estimate alp2
	    if (y == 0) {
	      salp1 = 1;
	      calp1 = 0;
	      // cerr << "0 ";
	    } else if (y > - 0.027 && x > -1.09 && x < -0.91) {
	      // Near singular point we have
	      // solve t^3 - 2*a*t - 2 = 0
	      // where a = (x + 1)/|y|^(2/3), t = calp2/|y|^(1/3)
	      double
		a = (x + 1)/sq(cbrt(y)),
		a3 = sq(a)*a,
		disc = 27 - 8 * a3,
		v = 1;
	      if (disc >= 0) {
		double s = 4 * a3 - 27;
		s += (s > 0 ? 1 : -1) * 3 * sqrt(3.0) * sqrt(disc);
		s /= 4*a3;
		s = cbrt(s);
		v += s + 1/s;
	      } else {
		double ang = atan2(3 * sqrt(3.0) * sqrt(-disc), 4 * a3 - 27) +
		  2 * Constants::pi();
		v += 2 * cos(ang/3);
	      }
	      calp1 = cbrt(-y) * -3 / a / v;
	      salp1 = sqrt(1 - sq(calp1));
	      // cerr << "1 ";
	    } else {
	      salp1 = 0;
	      calp1 = 1;
	      // cerr << "2 ";
	    }
	    // cerr << x + 1 << " " << y << " " << salp1 << " " << calp1 << " ";
	    for (unsigned i = 0; i < 30; ++i) {
	      ++iterx;
	      double
		v = calp1 * (salp1 + x) - y * salp1,
		dv = - calp1 * y - salp1 * x + (calp1 - salp1) * (calp1 + salp1),
		da = -v/dv,
		sda = sin(da),
		cda = cos(da),
		nsalp1 = salp1 * cda + calp1 * sda;
	      if (v == 0)
		break;
	      calp1 = max(0.0, calp1 * cda - salp1 * sda);
	      salp1 = max(0.0, nsalp1);
	      SinCosNorm(salp1, calp1);
	      if (abs(da) < tol1)
		break;
	    }
	    // cerr << iterx << " " << salp1 << " " << calp1 << "\n";
	    // estimate lam12
	    double r = hypot(y, salp1 + x) * chicrita * salp1;
	    // chi12 = pi - chicrita * r * salp1
	    schi12 = sin(r);
	    cchi12 = -cos(r);
	    salp1 = cbet2 * schi12;
	    calp1 = sbet12a - cbet2 * sbet1 * sq(schi12) / (1 - cchi12);
	    SinCosNorm(salp1, calp1);
	  }
	}
      }
	/*
	

      double sig120 =
	atan2(hypot(cbet2 * schi12,
),
	      sbet1 * sbet2 + cbet1 *cbet2 * cchi12)/Constants::degree();

      double
	chicrita = -cbet1 * dlamScale(_f, sq(sbet1)) * Constants::pi(),
	chicrit = Constants::pi() - chicrita;
      double xx = (chi12 - Constants::pi())/chicrita,
	yy = sbet12a / (chicrita * cbet1);
      //      cerr << setprecision(10);
      //      cerr << atan2(-sbet1, cbet1)/Constants::degree() << " "
      //      	   << chicrita / Constants::degree() << " "
      //      	   << xx << " " << yy << " ";

      //salp1 = min(1.0, (Constants::pi() - chi12) / chicrita);
      //calp1 = - sqrt(1 - sq(salp1));
      if(true) {
      if (chi12 == chicrit && cbet1 == cbet2 && sbet2 == -sbet1) {
	salp1 = 1; calp1 = 0;	// The singular point
	// This leads to
	//
	// sig12 = Constants::pi(); ssig1 = -1; salp2 = ssig2 = 1;
	// calp2 = csig1 = csig2 = 0; u2 = sq(sbet1) * _ep2;
	//
	// But we let Newton's method proceed so that we have fewer special
	// cases in the code.
      } else if (xx > -2 && yy > -2) {
	salp1 = 0;
	calp1 = 1;
	for (unsigned i = 0; i < 10; ++i) {
	  double
	    v = calp1 * (salp1 + xx) - yy * salp1,
	    dv = -calp1 * yy - salp1 * xx - sq(salp1) + sq(calp1),
	    da = -v/dv;
	  salp1 = max(0.0, salp1 + calp1 * da);
	  calp1 = max(0.0, calp1 - salp1 * da);
	  SinCosNorm(salp1, calp1);
	}
	calp1 *= -1;
	if (xx < -1 && yy > (1+xx)/1000000 ) {
	  calp1 = eps2;
	  salp1 = 1;
	}
      } else if (false && chi12 > chicrit && cbet12a > 0 && sbet12a > - chicrita) {
	salp1 = min(1.0, (Constants::pi() - chi12) / chicrita);
	calp1 = - sqrt(1 - sq(salp1));
      } else if (false && chi12 > Constants::pi() - 2 * chicrita &&
		 cbet12a > 0 && sbet12a > - 2 * chicrita) {
	salp1 = 1;
	calp1 = sbet2 <= 0 ? -eps2 : eps2;
      } else {
	if (false) {
	salp1 = 0;
	calp1 = 1;
	for (unsigned i = 0; i < 10; ++i) {
	  double
	    v = calp1 * (salp1 + xx) - yy * salp1,
	    dv = -calp1 * yy - salp1 * xx - sq(salp1) + sq(calp1),
	    da = -v/dv;
	  salp1 = max(0.0, salp1 + calp1 * da);
	  calp1 = max(0.0, calp1 - salp1 * da);
	  SinCosNorm(salp1, calp1);
	}
	double rr=max(eps2, hypot(yy, salp1 + xx) * chicrita * salp1);
	// chi12 = pi - chicrita * rr * salp1
	schi12 = sin(rr);
	cchi12 = -cos(rr);
	}

	salp1 = cbet2 * schi12;
	// calp1 = sbet2 * cbet1 - cbet2 * sbet1 * cchi12;
	// _f1 / n1 gives ellipsoid correction for short distances.
	calp1 = cchi12 >= 0 ?
	  sbet12 * _f1 / n1 + cbet2 * sbet1 * sq(schi12) / (1 + cchi12) :
	  sbet12a - cbet2 * sbet1 * sq(schi12) / (1 - cchi12);
	// N.B. ssig1 = hypot(salp1, calp1) (before normalization)
	SinCosNorm(salp1, calp1);
      }
	}
	*/
      double sig12, ssig1, csig1, ssig2, csig2, u2;

      // Newton's method
      for (unsigned i = 0, trip = 0; i < 50; ++i) {
	double dv;
	double v = Chi12(sbet1, cbet1, sbet2, cbet2,
			 salp1, calp1, 
			 salp2, calp2,
			 sig12, ssig1, csig1, ssig2, csig2,
			 u2, trip < 1, dv, c) - chi12;
	if (abs(v) <= eps2 || !(trip < 1))
	  break;
	double
	  dalp1 = -v/dv;
#if 1
	double
	  sdalp1 = sin(dalp1), cdalp1 = cos(dalp1),
	  nsalp1 = salp1 * cdalp1 + calp1 * sdalp1;
	calp1 = calp1 * cdalp1 - salp1 * sdalp1;
	salp1 = max(0.0, nsalp1);
#else
	 calp1 -=  salp1 * dalp1;
	 salp1 +=  calp1 * dalp1;
	 salp1 = max(0.0, salp1);
#endif
	SinCosNorm(salp1, calp1);
	if (abs(v) < tol) ++trip;
      }
	
      tauCoeff(u2, c);
      s12 =  _b * tauScale(u2) *
	(sig12 + (SinSeries(ssig2, csig2, c, maxpow_taucoef) -
		  SinSeries(ssig1, csig1, c, maxpow_taucoef)));

      //      cerr << sig12 / Constants::degree() << " " << extlam12 << " ";
      //      cerr << lon12 << " ";
      //      cerr << sig120 << "\n";




    }

    // Convert calp, salp to head accounting for
    // lonsign, swapp, latsign.  The minus signs up result in [-180, 180).

    if (swapp < 0) {
      swap(salp1, salp2);
      swap(calp1, calp2);
    }

    // minus signs give range [-180, 180). 0- converts -0 to +0.
    azi1 = 0-atan2(- swapp * lonsign * salp1,
		   + swapp * latsign * calp1) / Constants::degree();
    azi2 = 0-atan2(- azi2sense * swapp * lonsign * salp2,
		   + azi2sense * swapp * latsign * calp2) / Constants::degree();
    return;
  }

  double Geodesic::Chi12(double sbet1, double cbet1,
			 double sbet2, double cbet2,
			 double salp1, double calp1,
			 double& salp2, double& calp2,
			 double& sig12,
			 double& ssig1, double& csig1,
			 double& ssig2, double& csig2,
			 double& u2,
			 bool diffp, double& dchi12, double c[])
    const throw() {

#if ITER
    if (diffp) ++iter;
#endif
    if (sbet1 == 0 && calp1 == 0)
      // Break degeneracy of equatorial line.  This cases has already been
      // handled.
      calp1 = -eps2;

    double
      // sin(alp1) * cos(bet1) = sin(alp0),
      salp0 = salp1 * cbet1,
      calp0 = hypot(calp1, salp1 * sbet1); // calp0 > 0

    double slam1, clam1, slam2, clam2, lam12, chi12, mu;
    // tan(bet1) = tan(sig1) * cos(alp1)
    // tan(lam1) = sin(alp0) * tan(sig1) = tan(lam1)=tan(alp1)*sin(bet1)
    ssig1 = sbet1; slam1 = salp0 * sbet1;
    csig1 = clam1 = calp1 * cbet1;
    SinCosNorm(ssig1, csig1);
    SinCosNorm(slam1, clam1);
    /*
    ssig1 = sbet1; csig1 = calp1 * cbet1;
    SinCosNorm(ssig1, csig1);
    slam1 = salp1 * sbet1; clam1 = calp1;
    SinCosNorm(slam1, clam1);
    */

    // Enforce symmetries in the case abs(bet2) = -bet1.  Need to be careful
    // about this case, since this can yield singularities in the Newton
    // iteration.
    // sin(alp2) * cos(bet2) = sin(alp0),
    salp2 = cbet2 != cbet1 ? salp0 / cbet2 : salp1;
    // calp2 = sqrt(1 - sq(salp2))
    //       = sqrt(sq(calp0) - sq(sbet2)) / cbet2
    // and subst for calp0 and rearrange to give (choose positive sqrt
    // to give alp2 in [0, pi/2]).
    calp2 = cbet2 != cbet1 || abs(sbet2) != -sbet1 ?
      sqrt(sq(calp1 * cbet1) + (cbet1 < -sbet1 ?
				(cbet2 - cbet1) * (cbet1 + cbet2) :
				(sbet1 - sbet2) * (sbet1 + sbet2))) / cbet2 :
      abs(calp1);
    // tan(bet2) = tan(sig2) * cos(alp2)
    // tan(lam2) = sin(alp0) * tan(sig2).
    ssig2 = sbet2; slam2 = salp0 * sbet2;
    csig2 = clam2 = calp2 * cbet2;
    SinCosNorm(ssig2, csig2);
    SinCosNorm(slam2, clam2);

    // sig12 = sig2 - sig1, limit to [0, pi]
    sig12 = atan2(max(csig1 * ssig2 - ssig1 * csig2, 0.0),
		  csig1 * csig2 + ssig1 * ssig2);

    // lam12 = lam2 - lam1, limit to [0, pi]
    lam12 = atan2(max(clam1 * slam2 - slam1 * clam2, 0.0),
		  clam1 * clam2 + slam1 * slam2);
    double eta12, lamscale;
    mu = sq(calp0);
    dlamCoeff(_f, mu, c);
    eta12 = SinSeries(ssig2, csig2, c, maxpow_lamcoef) -
      SinSeries(ssig1, csig1, c, maxpow_lamcoef);
    lamscale = dlamScale(_f, mu),
    chi12 = lam12 + salp0 * lamscale * (sig12 + eta12);

    // XXX extlam12 = lam12 / Constants::degree();
    if (diffp) {
      double dalp0, dsig1, dlam1, dalp2, dsig2, dlam2;
      // Differentiate sin(alp) * cos(bet) = sin(alp0),
      dalp0 = cbet1 * calp1 / calp0;
      dalp2 = calp2 != 0 ? calp1 * cbet1/ (calp2 * cbet2) :
	calp1 >= 0 ? 1 : -1;
      // Differentiate tan(bet) = tan(sig) * cos(alp) and clear
      // calp from the denominator with tan(alp0)=cos(sig)*tan(alp),
      dsig1 = ssig1 * salp0 / calp0;
      dsig2 = ssig2 * salp0 / calp0 * dalp2;
      // Differentiate tan(lam) = sin(alp0) * tan(sig).  Substitute
      //   tan(sig) = tan(bet) / cos(alp) = tan(lam) / sin(alp0)
      //   cos(lam) / cos(sig) = 1 / cos(bet)
      // to give
      dlam1 = (sbet1 * sq(clam1) + slam1 * salp0 / (calp0 * cbet1));
      dlam2 = (sbet2 * sq(clam2) + slam2 * salp0 / (calp0 * cbet2)) * dalp2;

      double deta12, dmu, dlamscale, dchisig;
      dlamCoeffmu(_f, mu, c);
      dmu = - 2 * calp0 * salp0 * dalp0;
      deta12 = dmu * (SinSeries(ssig2, csig2, c, maxpow_lamcoef) -
		      SinSeries(ssig1, csig1, c, maxpow_lamcoef));
      dlamscale = dlamScalemu(_f, mu) * dmu;

      // Derivative of salp0 * lamscale * (sig + eta) wrt sig.  This
      // is from integral form of this expression.
      dchisig =  - _e2 * salp0 *
	(dsig2 / (sqrt(1 - _e2 * (1 - mu * sq(ssig2))) + 1) -
	 dsig1 / (sqrt(1 - _e2 * (1 - mu * sq(ssig1))) + 1)) ;

      dchi12 =
	(dlam2 - dlam1) + dchisig +
	// Derivative wrt mu
	(dalp0 * calp0 * lamscale + salp0 * dlamscale) * (sig12 + eta12) +
	salp0 * lamscale * deta12;
    }

#if ALTAZI
    salp2 = hypot(salp0, calp0 * slam2);
    calp2 = calp0 * clam2;
#endif
    u2 = mu * _ep2;
    return chi12;
  }

  GeodesicLine::GeodesicLine(const Geodesic& g,
			     double lat1, double lon1, double azi1) throw() {
    azi1 = Geodesic::AngNormalize(azi1);
    // Normalize azimuth at poles.  Evaluate azimuths at lat = +/- (90 - eps).
    if (lat1 == 90) {
      lon1 -= azi1 - (azi1 >= 0 ? 180 : -180);
      azi1 = -180;
    } else if (lat1 == -90) {
      lon1 += azi1;
      azi1 = 0;
    }
    // Guard against underflow in salp0
    azi1 = Geodesic::AngRound(azi1);
    lon1 = Geodesic::AngNormalize(lon1);
    _bsign = azi1 >= 0 ? 1 : -1;
    azi1 *= _bsign;
    _lat1 = lat1;
    _lon1 = lon1;
    _azi1 = azi1;
    _f1 = g._f1;
    // alp1 is in [0, pi]
    double
      alp1 = azi1 * Constants::degree(),
      // Enforce sin(pi) == 0 and cos(pi/2) == 0.  Better to face the ensuing
      // problems directly than to skirt them.
      salp1 = azi1 == 180 ? 0 : sin(alp1),
      calp1 = azi1 ==  90 ? 0 : cos(alp1);
    double cbet1, sbet1, phi;
    phi = lat1 * Constants::degree();
    // Ensure cbet1 = +eps at poles
    sbet1 = _f1 * sin(phi);
    cbet1 = abs(lat1) == 90 ? Geodesic::eps2 : cos(phi);
    Geodesic::SinCosNorm(sbet1, cbet1);

    // Evaluate alp0 from sin(alp1) * cos(bet1) = sin(alp0),
    _salp0 = salp1 * cbet1; // alp0 in [0, pi/2 - |bet1|]
    // Alt: calp0 = hypot(sbet1, calp1 * cbet1).  The following
    // is slightly better (consider the case salp1 = 0).
    _calp0 = Geodesic::hypot(calp1, salp1 * sbet1);
    // Evaluate sig with tan(bet1) = tan(sig1) * cos(alp1).
    // sig = 0 is nearest northward crossing of equator.
    // With bet1 = 0, alp1 = pi/2, we have sig1 = 0 (equatorial line).
    // With bet1 =  pi/2, alp1 = -pi, sig1 =  pi/2
    // With bet1 = -pi/2, alp1 =  0 , sig1 = -pi/2
    // Evaluate lam1 with tan(lam1) = sin(alp0) * tan(sig1).
    // With alp0 in (0, pi/2], quadrants for sig and lam coincide.
    // No atan2(0,0) ambiguity at poles sce cbet1 = +eps.
    // With alp0 = 0, lam1 = 0 for alp1 = 0, lam1 = pi for alp1 = pi.
    _ssig1 = sbet1; _slam1 = _salp0 * sbet1;
    _csig1 = _clam1 = sbet1 != 0 || calp1 != 0 ? cbet1 * calp1 : 1;

    Geodesic::SinCosNorm(_ssig1, _csig1); // sig1 in (-pi, pi]
    Geodesic::SinCosNorm(_slam1, _clam1);
    /*
    _ssig1 = sbet1; _csig1 = sbet1 != 0 || calp1 != 0 ? cbet1 * calp1 : 1;
    Geodesic::SinCosNorm(_ssig1, _csig1); // sig1 in (-pi, pi]
    _slam1 = salp1 * sbet1; _clam1 = sbet1 != 0 || calp1 != 0 ? calp1 : 1;
    Geodesic::SinCosNorm(_slam1, _clam1);
    */

    double
      mu = Geodesic::sq(_calp0),
      u2 = mu * g._ep2;

    _sScale =  g._b * Geodesic::tauScale(u2);
    Geodesic::tauCoeff(u2, _sigCoeff);
    _dtau1 = Geodesic::SinSeries(_ssig1, _csig1, _sigCoeff, maxpow_taucoef);
    {
      double s = sin(_dtau1), c = cos(_dtau1);
      // tau1 = sig1 + dtau1
      _stau1 = _ssig1 * c + _csig1 * s;
      _ctau1 = _csig1 * c - _ssig1 * s;
    }
    Geodesic::sigCoeff(u2, _sigCoeff);
    // Not necessary because sigCoeff reverts tauCoeff
    //    _dtau1 = -SinSeries(_stau1, _ctau1, _sigCoeff, maxpow_sigcoef);

    _dlamScale = _salp0 * Geodesic::dlamScale(g._f, mu);
    Geodesic::dlamCoeff(g._f, mu, _dlamCoeff);
    _dchi1 = Geodesic::SinSeries(_ssig1, _csig1, _dlamCoeff, maxpow_lamcoef);
  }

  void GeodesicLine::Position(double s12,
			      double& lat2, double& lon2, double& azi2)
  const throw() {
    if (_sScale == 0)
      // Uninitialized
      return;
    double tau12, sig12, lam12, chi12, lon12, s, c;
    double ssig2, csig2, sbet2, cbet2, slam2, clam2, salp2, calp2;
    tau12 = s12 / _sScale;
    s = sin(tau12); c = cos(tau12);
    sig12 = tau12 + (_dtau1 +
		     // tau2 = tau1 + tau12
		     Geodesic::SinSeries(_stau1 * c + _ctau1 * s,
					 _ctau1 * c - _stau1 * s,
					 _sigCoeff, maxpow_sigcoef));
    s = sin(sig12); c = cos(sig12);
    // sig2 = sig1 + sig12
    ssig2 = _ssig1 * c + _csig1 * s;
    csig2 = _csig1 * c - _ssig1 * s;
    // sin(bet2) = cos(alp0) * sin(sig2)
    sbet2 = _calp0 * ssig2;
    // Alt: cbet2 = hypot(csig2, salp0 * ssig2);
    cbet2 = Geodesic::hypot(_salp0, _calp0 * csig2);
    // tan(lam2) = sin(alp0) * tan(sig2)
    slam2 = _salp0 * ssig2; clam2 = csig2;  // No need to normalize
    // tan(alp0) = cos(sig2)*tan(alp2)
    salp2 = _salp0; calp2 = _calp0 * csig2; // No need to normalize
    // lam12 = lam2 - lam1
    lam12 = atan2(slam2 * _clam1 - clam2 * _slam1,
		  clam2 * _clam1 + slam2 * _slam1);
    chi12 = lam12 + _dlamScale *
      ( sig12 +
	(Geodesic::SinSeries(ssig2, csig2, _dlamCoeff, maxpow_lamcoef)  - _dchi1));
    lon12 = _bsign * chi12 / Constants::degree();
    // Can't use AngNormalize because longitude might have wrapped multiple
    // times.
    lon12 = lon12 - 360 * floor(lon12/360 + 0.5);
    lat2 = atan2(sbet2, _f1 * cbet2) / Constants::degree();
    lon2 = Geodesic::AngNormalize(_lon1 + lon12);
    // minus signs give range [-180, 180). 0- converts -0 to +0.
    azi2 = 0-atan2(- Geodesic::azi2sense * _bsign * salp2,
		   + Geodesic::azi2sense * calp2) / Constants::degree();
  }

  // The following is code generated by Maxima.  Minor edits have been made:
  // (1) remove blackslash-newline line continuations; (2) promote integers of
  // 10 digits or more to doubles; (3) rudimentary line-breaking.

#if !(MINPOW >= 1 && MAXPOW <= 8 && MINPOW <= MAXPOW)
#error Bad MINPOW or MAXPOW
#endif

  // The scale factor to convert tau to s / b
  double Geodesic::tauScale(double u2) throw() {
#if MAXPOW_TAUSC <= 1
    return (u2+4)/4;
#elif MAXPOW_TAUSC == 2
    return ((16-3*u2)*u2+64)/64;
#elif MAXPOW_TAUSC == 3
    return (u2*(u2*(5*u2-12)+64)+256)/256;
#elif MAXPOW_TAUSC == 4
    return (u2*(u2*((320-175*u2)*u2-768)+4096)+16384)/16384;
#elif MAXPOW_TAUSC == 5
    return (u2*(u2*(u2*(u2*(441*u2-700)+1280)-3072)+16384)+65536)/65536;
#elif MAXPOW_TAUSC == 6
    return (u2*(u2*(u2*(u2*((7056-4851*u2)*u2-11200)+20480)-49152)+262144)+
      1048576)/1048576;
#elif MAXPOW_TAUSC == 7
    return (u2*(u2*(u2*(u2*(u2*(u2*(14157*u2-19404)+28224)-44800)+81920)-
      196608)+1048576)+4194304)/4194304;
#elif MAXPOW_TAUSC >= 8
    return (u2*(u2*(u2*(u2*(u2*(u2*((3624192-2760615*u2)*u2-4967424)+7225344)-
      11468800)+20971520)-50331648)+268435456)+1073741824.0)/1073741824.0;
#endif
  }

  // Coefficients of sine series to convert sigma to tau (a reversion of
  // tauCoeff).
  void Geodesic::tauCoeff(double u2, double c[]) throw() {
    double t = u2;
#if MAXPOW_TAUCOEF <= 1
    c[0] = -t/8;
#elif MAXPOW_TAUCOEF == 2
    c[0] = t*(u2-2)/16;
    t *= u2;
    c[1] = -t/256;
#elif MAXPOW_TAUCOEF == 3
    c[0] = t*((64-37*u2)*u2-128)/1024;
    t *= u2;
    c[1] = t*(u2-1)/256;
    t *= u2;
    c[2] = -t/3072;
#elif MAXPOW_TAUCOEF == 4
    c[0] = t*(u2*(u2*(47*u2-74)+128)-256)/2048;
    t *= u2;
    c[1] = t*((32-27*u2)*u2-32)/8192;
    t *= u2;
    c[2] = t*(3*u2-2)/6144;
    t *= u2;
    c[3] = -5*t/131072;
#elif MAXPOW_TAUCOEF == 5
    c[0] = t*(u2*(u2*((752-511*u2)*u2-1184)+2048)-4096)/32768;
    t *= u2;
    c[1] = t*(u2*(u2*(22*u2-27)+32)-32)/8192;
    t *= u2;
    c[2] = t*((384-423*u2)*u2-256)/786432;
    t *= u2;
    c[3] = t*(10*u2-5)/131072;
    t *= u2;
    c[4] = -7*t/1310720;
#elif MAXPOW_TAUCOEF == 6
    c[0] = t*(u2*(u2*(u2*(u2*(731*u2-1022)+1504)-2368)+4096)-8192)/65536;
    t *= u2;
    c[1] = t*(u2*(u2*((22528-18313*u2)*u2-27648)+32768)-32768)/8388608;
    t *= u2;
    c[2] = t*(u2*(u2*(835*u2-846)+768)-512)/1572864;
    t *= u2;
    c[3] = t*((160-217*u2)*u2-80)/2097152;
    t *= u2;
    c[4] = t*(35*u2-14)/2621440;
    t *= u2;
    c[5] = -7*t/8388608;
#elif MAXPOW_TAUCOEF == 7
    c[0] = t*(u2*(u2*(u2*(u2*((374272-278701*u2)*u2-523264)+770048)-1212416)+
      2097152)-4194304)/33554432;
    t *= u2;
    c[1] = t*(u2*(u2*(u2*(u2*(15003*u2-18313)+22528)-27648)+32768)-32768)/
      8388608;
    t *= u2;
    c[2] = t*(u2*(u2*((53440-50241*u2)*u2-54144)+49152)-32768)/100663296;
    t *= u2;
    c[3] = t*(u2*(u2*(251*u2-217)+160)-80)/2097152;
    t *= u2;
    c[4] = t*((2240-3605*u2)*u2-896)/167772160;
    t *= u2;
    c[5] = t*(21*u2-7)/8388608;
    t *= u2;
    c[6] = -33*t/234881024;
#elif MAXPOW_TAUCOEF >= 8
    c[0] = t*(u2*(u2*(u2*(u2*(u2*(u2*(428731*u2-557402)+748544)-1046528)+
      1540096)-2424832)+4194304)-8388608)/67108864;
    t *= u2;
    c[1] = t*(u2*(u2*(u2*(u2*((480096-397645*u2)*u2-586016)+720896)-884736)+
      1048576)-1048576)/268435456;
    t *= u2;
    c[2] = t*(u2*(u2*(u2*(u2*(92295*u2-100482)+106880)-108288)+98304)-65536)/
      201326592;
    t *= u2;
    c[3] = t*(u2*(u2*((128512-136971*u2)*u2-111104)+81920)-40960)/1073741824.0;
    t *= u2;
    c[4] = t*(u2*(u2*(9555*u2-7210)+4480)-1792)/335544320;
    t *= u2;
    c[5] = t*((672-1251*u2)*u2-224)/268435456;
    t *= u2;
    c[6] = t*(231*u2-66)/469762048;
    t *= u2;
    c[7] = -429*t/17179869184.0;
#endif
  }

  // Coefficients of sine series to convert tau to sigma (a reversion of
  // tauCoeff).
  void Geodesic::sigCoeff(double u2, double d[]) throw() {
    double t = u2;
#if MAXPOW_SIGCOEF <= 1
    d[0] = t/8;
#elif MAXPOW_SIGCOEF == 2
    d[0] = t*(2-u2)/16;
    t *= u2;
    d[1] = 5*t/256;
#elif MAXPOW_SIGCOEF == 3
    d[0] = t*(u2*(71*u2-128)+256)/2048;
    t *= u2;
    d[1] = t*(5-5*u2)/256;
    t *= u2;
    d[2] = 29*t/6144;
#elif MAXPOW_SIGCOEF == 4
    d[0] = t*(u2*((142-85*u2)*u2-256)+512)/4096;
    t *= u2;
    d[1] = t*(u2*(383*u2-480)+480)/24576;
    t *= u2;
    d[2] = t*(58-87*u2)/12288;
    t *= u2;
    d[3] = 539*t/393216;
#elif MAXPOW_SIGCOEF == 5
    d[0] = t*(u2*(u2*(u2*(20797*u2-32640)+54528)-98304)+196608)/1572864;
    t *= u2;
    d[1] = t*(u2*((383-286*u2)*u2-480)+480)/24576;
    t *= u2;
    d[2] = t*(u2*(2907*u2-2784)+1856)/393216;
    t *= u2;
    d[3] = t*(539-1078*u2)/393216;
    t *= u2;
    d[4] = 3467*t/7864320;
#elif MAXPOW_SIGCOEF == 6
    d[0] = t*(u2*(u2*(u2*((41594-27953*u2)*u2-65280)+109056)-196608)+393216)/
      3145728;
    t *= u2;
    d[1] = t*(u2*(u2*(u2*(429221*u2-585728)+784384)-983040)+983040)/50331648;
    t *= u2;
    d[2] = t*(u2*((5814-5255*u2)*u2-5568)+3712)/786432;
    t *= u2;
    d[3] = t*(u2*(111407*u2-86240)+43120)/31457280;
    t *= u2;
    d[4] = t*(6934-17335*u2)/15728640;
    t *= u2;
    d[5] = 38081*t/251658240;
#elif MAXPOW_SIGCOEF == 7
    d[0] = t*(u2*(u2*(u2*(u2*(u2*(7553633*u2-10733952)+15972096)-25067520)+
      41877504)-75497472)+150994944)/1207959552.0;
    t *= u2;
    d[1] = t*(u2*(u2*(u2*((429221-314863*u2)*u2-585728)+784384)-983040)+
      983040)/50331648;
    t *= u2;
    d[2] = t*(u2*(u2*(u2*(1133151*u2-1345280)+1488384)-1425408)+950272)/
      201326592;
    t *= u2;
    d[3] = t*(u2*((111407-118621*u2)*u2-86240)+43120)/31457280;
    t *= u2;
    d[4] = t*(u2*(2563145*u2-1664160)+665664)/1509949440.0;
    t *= u2;
    d[5] = t*(38081-114243*u2)/251658240;
    t *= u2;
    d[6] = 459485*t/8455716864.0;
#elif MAXPOW_SIGCOEF >= 8
    d[0] = t*(u2*(u2*(u2*(u2*(u2*((15107266-11062823*u2)*u2-21467904)+
      31944192)-50135040)+83755008)-150994944)+301989888)/2415919104.0;
    t *= u2;
    d[1] = t*(u2*(u2*(u2*(u2*(u2*(112064929*u2-151134240)+206026080)-
      281149440)+376504320)-471859200)+471859200)/24159191040.0;
    t *= u2;
    d[2] = t*(u2*(u2*(u2*((2266302-1841049*u2)*u2-2690560)+2976768)-2850816)+
      1900544)/402653184;
    t *= u2;
    d[3] = t*(u2*(u2*(u2*(174543337*u2-182201856)+171121152)-132464640)+
      66232320)/48318382080.0;
    t *= u2;
    d[4] = t*(u2*((5126290-6292895*u2)*u2-3328320)+1331328)/3019898880.0;
    t *= u2;
    d[5] = t*(u2*(45781749*u2-25590432)+8530144)/56371445760.0;
    t *= u2;
    d[6] = t*(918970-3216395*u2)/16911433728.0;
    t *= u2;
    d[7] = 109167851*t/5411658792960.0;
#endif
  }

  double Geodesic::dlamScale(double f, double mu) throw() {
#if MAXPOW_LAMSC <= 1
    double g = -1;
#elif MAXPOW_LAMSC == 2
    double g = (f*mu-4)/4;
#elif MAXPOW_LAMSC == 3
    double g = (f*(f*(4-3*mu)*mu+4*mu)-16)/16;
#elif MAXPOW_LAMSC == 4
    double g = (f*(f*(f*mu*(mu*(25*mu-54)+32)+(32-24*mu)*mu)+32*mu)-128)/128;
#elif MAXPOW_LAMSC == 5
    double g = (f*(f*(f*(f*mu*(mu*((720-245*mu)*mu-720)+256)+mu*(mu*(200*mu-
      432)+256))+(256-192*mu)*mu)+256*mu)-1024)/1024;
#elif MAXPOW_LAMSC == 6
    double g = (f*(f*(f*(f*(f*mu*(mu*(mu*(mu*(1323*mu-4900)+6800)-4224)+1024)+
      mu*(mu*((2880-980*mu)*mu-2880)+1024))+mu*(mu*(800*mu-1728)+1024))+(1024-
      768*mu)*mu)+1024*mu)-4096)/4096;
#elif MAXPOW_LAMSC == 7
    double g = (f*(f*(f*(f*(f*(f*mu*(mu*(mu*(mu*((34020-7623*mu)*mu-60200)+
      52800)-23040)+4096)+mu*(mu*(mu*(mu*(5292*mu-19600)+27200)-16896)+4096))+
      mu*(mu*((11520-3920*mu)*mu-11520)+4096))+mu*(mu*(3200*mu-6912)+4096))+
      (4096-3072*mu)*mu)+4096*mu)-16384)/16384;
#elif MAXPOW_LAMSC >= 8
    double g = (f*(f*(f*(f*(f*(f*(f*mu*(mu*(mu*(mu*(mu*(mu*(184041*mu-960498)+
      2063880)-2332400)+1459200)-479232)+65536)+mu*(mu*(mu*(mu*((544320-121968*
      mu)*mu-963200)+844800)-368640)+65536))+mu*(mu*(mu*(mu*(84672*mu-313600)+
      435200)-270336)+65536))+mu*(mu*((184320-62720*mu)*mu-184320)+65536))+mu*
      (mu*(51200*mu-110592)+65536))+(65536-49152*mu)*mu)+65536*mu)-262144)/
      262144;
#endif
    return f * g;
  }

  double Geodesic::dlamScalemu(double f, double mu) throw() {
#if MAXPOW_LAMSC <= 1
    double h = 0;
#elif MAXPOW_LAMSC == 2
    double h = 1/4;
#elif MAXPOW_LAMSC == 3
    double h = (f*(2-3*mu)+2)/8;
#elif MAXPOW_LAMSC == 4
    double h = (f*(f*(mu*(75*mu-108)+32)-48*mu+32)+32)/128;
#elif MAXPOW_LAMSC == 5
    double h = (f*(f*(f*(mu*((540-245*mu)*mu-360)+64)+mu*(150*mu-216)+64)-96*
      mu+64)+64)/256;
#elif MAXPOW_LAMSC == 6
    double h = (f*(f*(f*(f*(mu*(mu*(mu*(6615*mu-19600)+20400)-8448)+1024)+mu*
      ((8640-3920*mu)*mu-5760)+1024)+mu*(2400*mu-3456)+1024)-1536*mu+1024)+
      1024)/4096;
#elif MAXPOW_LAMSC == 7
    double h = (f*(f*(f*(f*(f*(mu*(mu*(mu*((85050-22869*mu)*mu-120400)+79200)-
      23040)+2048)+mu*(mu*(mu*(13230*mu-39200)+40800)-16896)+2048)+mu*((17280-
      7840*mu)*mu-11520)+2048)+mu*(4800*mu-6912)+2048)-3072*mu+2048)+2048)/8192;
#elif MAXPOW_LAMSC >= 8
    double h = (f*(f*(f*(f*(f*(f*(mu*(mu*(mu*(mu*(mu*(1288287*mu-5762988)+
      10319400)-9329600)+4377600)-958464)+65536)+mu*(mu*(mu*((2721600-731808*
      mu)*mu-3852800)+2534400)-737280)+65536)+mu*(mu*(mu*(423360*mu-1254400)+
      1305600)-540672)+65536)+mu*((552960-250880*mu)*mu-368640)+65536)+mu*
      (153600*mu-221184)+65536)-98304*mu+65536)+65536)/262144;
#endif
    return h * sq(f);
  }

  void Geodesic::dlamCoeff(double f, double mu, double e[]) throw() {
    double s = f*mu, t = s;
#if MAXPOW_LAMCOEF <= 1
    e[0] = t/8;
#elif MAXPOW_LAMCOEF == 2
    e[0] = (f*(4-3*mu)+4)*t/32;
    t *= s;
    e[1] = t/64;
#elif MAXPOW_LAMCOEF == 3
    e[0] = (f*(f*(mu*(51*mu-112)+64)-48*mu+64)+64)*t/512;
    t *= s;
    e[1] = (f*(18-13*mu)+8)*t/512;
    t *= s;
    e[2] = 5*t/1536;
#elif MAXPOW_LAMCOEF == 4
    e[0] = (f*(f*(f*(mu*((764-255*mu)*mu-768)+256)+mu*(204*mu-448)+256)-192*mu+
      256)+256)*t/2048;
    t *= s;
    e[1] = (f*(f*(mu*(79*mu-190)+120)-52*mu+72)+32)*t/2048;
    t *= s;
    e[2] = (f*(72-51*mu)+20)*t/6144;
    t *= s;
    e[3] = 7*t/8192;
#elif MAXPOW_LAMCOEF == 5
    e[0] = (f*(f*(f*(f*(mu*(mu*(mu*(701*mu-2646)+3724)-2304)+512)+mu*((1528-
      510*mu)*mu-1536)+512)+mu*(408*mu-896)+512)-384*mu+512)+512)*t/4096;
    t *= s;
    e[1] = (f*(f*(f*(mu*((1610-487*mu)*mu-1816)+704)+mu*(316*mu-760)+480)-208*
      mu+288)+128)*t/8192;
    t *= s;
    e[2] = (f*(f*(mu*(813*mu-2056)+1360)-408*mu+576)+160)*t/49152;
    t *= s;
    e[3] = (f*(70-49*mu)+14)*t/16384;
    t *= s;
    e[4] = 21*t/81920;
#elif MAXPOW_LAMCOEF == 6
    e[0] = (f*(f*(f*(f*(f*(mu*(mu*(mu*((74558-16411*mu)*mu-134064)+118720)-
      51200)+8192)+mu*(mu*(mu*(11216*mu-42336)+59584)-36864)+8192)+mu*((24448-
      8160*mu)*mu-24576)+8192)+mu*(6528*mu-14336)+8192)-6144*mu+8192)+8192)*t/
      65536;
    t *= s;
    e[1] = (f*(f*(f*(f*(mu*(mu*(mu*(12299*mu-51072)+80360)-56960)+15360)+mu*
      ((25760-7792*mu)*mu-29056)+11264)+mu*(5056*mu-12160)+7680)-3328*mu+4608)+
      2048)*t/131072;
    t *= s;
    e[2] = (f*(f*(f*(mu*((10567-3008*mu)*mu-12712)+5280)+mu*(1626*mu-4112)+
      2720)-816*mu+1152)+320)*t/98304;
    t *= s;
    e[3] = (f*(f*(mu*(485*mu-1266)+860)-196*mu+280)+56)*t/65536;
    t *= s;
    e[4] = (f*(540-375*mu)+84)*t/327680;
    t *= s;
    e[5] = 11*t/131072;
#elif MAXPOW_LAMCOEF == 7
    e[0] = (f*(f*(f*(f*(f*(f*(mu*(mu*(mu*(mu*(mu*(803251*mu-4262272)+9306208)-
      10659328)+6707200)-2162688)+262144)+mu*(mu*(mu*((2385856-525152*mu)*mu-
      4290048)+3799040)-1638400)+262144)+mu*(mu*(mu*(358912*mu-1354752)+
      1906688)-1179648)+262144)+mu*((782336-261120*mu)*mu-786432)+262144)+mu*
      (208896*mu-458752)+262144)-196608*mu+262144)+262144)*t/2097152;
    t *= s;
    e[1] = (f*(f*(f*(f*(f*(mu*(mu*(mu*((1579066-317733*mu)*mu-3149568)+
      3154560)-1587200)+319488)+mu*(mu*(mu*(196784*mu-817152)+1285760)-911360)+
      245760)+mu*((412160-124672*mu)*mu-464896)+180224)+mu*(80896*mu-194560)+
      122880)-53248*mu+73728)+32768)*t/2097152;
    t *= s;
    e[2] = (f*(f*(f*(f*(mu*(mu*(mu*(346689*mu-1534256)+2588016)-1980928)+
      583680)+mu*((676288-192512*mu)*mu-813568)+337920)+mu*(104064*mu-263168)+
      174080)-52224*mu+73728)+20480)*t/6291456;
    t *= s;
    e[3] = (f*(f*(f*(mu*((123082-33633*mu)*mu-154232)+66640)+mu*(15520*mu-
      40512)+27520)-6272*mu+8960)+1792)*t/2097152;
    t *= s;
    e[4] = (f*(f*(mu*(35535*mu-94800)+65520)-12000*mu+17280)+2688)*t/10485760;
    t *= s;
    e[5] = (f*(1386-957*mu)+176)*t/2097152;
    t *= s;
    e[6] = 429*t/14680064;
#elif MAXPOW_LAMCOEF >= 8
    e[0] = (f*(f*(f*(f*(f*(f*(f*(mu*(mu*(mu*(mu*(mu*((30816920-5080225*mu)*mu-
      79065664)+110840000)-91205632)+43638784)-11010048)+1048576)+mu*(mu*(mu*
      (mu*(mu*(3213004*mu-17049088)+37224832)-42637312)+26828800)-8650752)+
      1048576)+mu*(mu*(mu*((9543424-2100608*mu)*mu-17160192)+15196160)-
      6553600)+1048576)+mu*(mu*(mu*(1435648*mu-5419008)+7626752)-4718592)+
      1048576)+mu*((3129344-1044480*mu)*mu-3145728)+1048576)+mu*(835584*mu-
      1835008)+1048576)-786432*mu+1048576)+1048576)*t/8388608;
    t *= s;
    e[1] = (f*(f*(f*(f*(f*(f*(mu*(mu*(mu*(mu*(mu*(2092939*mu-12074982)+
      29005488)-37129344)+26700800)-10207232)+1605632)+mu*(mu*(mu*((6316264-
      1270932*mu)*mu-12598272)+12618240)-6348800)+1277952)+mu*(mu*(mu*(787136*
      mu-3268608)+5143040)-3645440)+983040)+mu*((1648640-498688*mu)*mu-
      1859584)+720896)+mu*(323584*mu-778240)+491520)-212992*mu+294912)+131072)*
      t/8388608;
    t *= s;
    e[2] = (f*(f*(f*(f*(f*(mu*(mu*(mu*((13101384-2474307*mu)*mu-28018000)+
      30323072)-16658432)+3727360)+mu*(mu*(mu*(1386756*mu-6137024)+10352064)-
      7923712)+2334720)+mu*((2705152-770048*mu)*mu-3254272)+1351680)+mu*
      (416256*mu-1052672)+696320)-208896*mu+294912)+81920)*t/25165824;
    t *= s;
    e[3] = (f*(f*(f*(f*(mu*(mu*(mu*(273437*mu-1265846)+2238200)-1799088)+
      557760)+mu*((492328-134532*mu)*mu-616928)+266560)+mu*(62080*mu-162048)+
      110080)-25088*mu+35840)+7168)*t/8388608;
    t *= s;
    e[4] = (f*(f*(f*(mu*((1333160-353765*mu)*mu-1718160)+761600)+mu*(142140*mu-
      379200)+262080)-48000*mu+69120)+10752)*t/41943040;
    t *= s;
    e[5] = (f*(f*(mu*(39633*mu-107426)+75152)-11484*mu+16632)+2112)*t/25165824;
    t *= s;
    e[6] = (f*(16016-11011*mu)+1716)*t/58720256;
    t *= s;
    e[7] = 715*t/67108864;
#endif
  }

  void Geodesic::dlamCoeffmu(double f, double mu, double h[]) throw() {
    double s = f*mu, t = f;
#if MAXPOW_LAMCOEF <= 1
    h[0] = t/8;
#elif MAXPOW_LAMCOEF == 2
    h[0] = (f*(2-3*mu)+2)*t/16;
    t *= s;
    h[1] = t/32;
#elif MAXPOW_LAMCOEF == 3
    h[0] = (f*(f*(mu*(153*mu-224)+64)-96*mu+64)+64)*t/512;
    t *= s;
    h[1] = (f*(36-39*mu)+16)*t/512;
    t *= s;
    h[2] = 5*t/512;
#elif MAXPOW_LAMCOEF == 4
    h[0] = (f*(f*(f*(mu*((573-255*mu)*mu-384)+64)+mu*(153*mu-224)+64)-96*mu+
      64)+64)*t/512;
    t *= s;
    h[1] = (f*(f*(mu*(158*mu-285)+120)-78*mu+72)+32)*t/1024;
    t *= s;
    h[2] = (f*(18-17*mu)+5)*t/512;
    t *= s;
    h[3] = 7*t/2048;
#elif MAXPOW_LAMCOEF == 5
    h[0] = (f*(f*(f*(f*(mu*(mu*(mu*(3505*mu-10584)+11172)-4608)+512)+mu*((4584-
      2040*mu)*mu-3072)+512)+mu*(1224*mu-1792)+512)-768*mu+512)+512)*t/4096;
    t *= s;
    h[1] = (f*(f*(f*(mu*((6440-2435*mu)*mu-5448)+1408)+mu*(1264*mu-2280)+960)-
      624*mu+576)+256)*t/8192;
    t *= s;
    h[2] = (f*(f*(mu*(4065*mu-8224)+4080)-1632*mu+1728)+480)*t/49152;
    t *= s;
    h[3] = (f*(280-245*mu)+56)*t/16384;
    t *= s;
    h[4] = 21*t/16384;
#elif MAXPOW_LAMCOEF == 6
    h[0] = (f*(f*(f*(f*(f*(mu*(mu*(mu*((186395-49233*mu)*mu-268128)+178080)-
      51200)+4096)+mu*(mu*(mu*(28040*mu-84672)+89376)-36864)+4096)+mu*((36672-
      16320*mu)*mu-24576)+4096)+mu*(9792*mu-14336)+4096)-6144*mu+4096)+4096)*t/
      32768;
    t *= s;
    h[1] = (f*(f*(f*(f*(mu*(mu*(mu*(36897*mu-127680)+160720)-85440)+15360)+mu*
      ((51520-19480*mu)*mu-43584)+11264)+mu*(10112*mu-18240)+7680)-4992*mu+
      4608)+2048)*t/65536;
    t *= s;
    h[2] = (f*(f*(f*(mu*((52835-18048*mu)*mu-50848)+15840)+mu*(8130*mu-16448)+
      8160)-3264*mu+3456)+960)*t/98304;
    t *= s;
    h[3] = (f*(f*(mu*(1455*mu-3165)+1720)-490*mu+560)+112)*t/32768;
    t *= s;
    h[4] = (f*(270-225*mu)+42)*t/32768;
    t *= s;
    h[5] = 33*t/65536;
#elif MAXPOW_LAMCOEF == 7
    h[0] = (f*(f*(f*(f*(f*(f*(mu*(mu*(mu*(mu*(mu*(5622757*mu-25573632)+
      46531040)-42637312)+20121600)-4325376)+262144)+mu*(mu*(mu*((11929280-
      3150912*mu)*mu-17160192)+11397120)-3276800)+262144)+mu*(mu*(mu*(1794560*
      mu-5419008)+5720064)-2359296)+262144)+mu*((2347008-1044480*mu)*mu-
      1572864)+262144)+mu*(626688*mu-917504)+262144)-393216*mu+262144)+262144)*
      t/2097152;
    t *= s;
    h[1] = (f*(f*(f*(f*(f*(mu*(mu*(mu*((9474396-2224131*mu)*mu-15747840)+
      12618240)-4761600)+638976)+mu*(mu*(mu*(1180704*mu-4085760)+5143040)-
      2734080)+491520)+mu*((1648640-623360*mu)*mu-1394688)+360448)+mu*(323584*
      mu-583680)+245760)-159744*mu+147456)+65536)*t/2097152;
    t *= s;
    h[2] = (f*(f*(f*(f*(mu*(mu*(mu*(2426823*mu-9205536)+12940080)-7923712)+
      1751040)+mu*((3381440-1155072*mu)*mu-3254272)+1013760)+mu*(520320*mu-
      1052672)+522240)-208896*mu+221184)+61440)*t/6291456;
    t *= s;
    h[3] = (f*(f*(f*(mu*((738492-235431*mu)*mu-771160)+266560)+mu*(93120*mu-
      202560)+110080)-31360*mu+35840)+7168)*t/2097152;
    t *= s;
    h[4] = (f*(f*(mu*(49749*mu-113760)+65520)-14400*mu+17280)+2688)*t/2097152;
    t *= s;
    h[5] = (f*(8316-6699*mu)+1056)*t/2097152;
    t *= s;
    h[6] = 429*t/2097152;
#elif MAXPOW_LAMCOEF >= 8
    h[0] = (f*(f*(f*(f*(f*(f*(f*(mu*(mu*(mu*(mu*(mu*((53929610-10160450*mu)*mu-
      118598496)+138550000)-91205632)+32729088)-5505024)+262144)+mu*(mu*(mu*
      (mu*(mu*(5622757*mu-25573632)+46531040)-42637312)+20121600)-4325376)+
      262144)+mu*(mu*(mu*((11929280-3150912*mu)*mu-17160192)+11397120)-
      3276800)+262144)+mu*(mu*(mu*(1794560*mu-5419008)+5720064)-2359296)+
      262144)+mu*((2347008-1044480*mu)*mu-1572864)+262144)+mu*(626688*mu-
      917504)+262144)-393216*mu+262144)+262144)*t/2097152;
    t *= s;
    h[1] = (f*(f*(f*(f*(f*(f*(mu*(mu*(mu*(mu*(mu*(8371756*mu-42262437)+
      87016464)-92823360)+53401600)-15310848)+1605632)+mu*(mu*(mu*((18948792-
      4448262*mu)*mu-31495680)+25236480)-9523200)+1277952)+mu*(mu*(mu*(2361408*
      mu-8171520)+10286080)-5468160)+983040)+mu*((3297280-1246720*mu)*mu-
      2789376)+720896)+mu*(647168*mu-1167360)+491520)-319488*mu+294912)+
      131072)*t/4194304;
    t *= s;
    h[2] = (f*(f*(f*(f*(f*(mu*(mu*(mu*((22927422-4948614*mu)*mu-42027000)+
      37903840)-16658432)+2795520)+mu*(mu*(mu*(2426823*mu-9205536)+12940080)-
      7923712)+1751040)+mu*((3381440-1155072*mu)*mu-3254272)+1013760)+mu*
      (520320*mu-1052672)+522240)-208896*mu+221184)+61440)*t/6291456;
    t *= s;
    h[3] = (f*(f*(f*(f*(mu*(mu*(mu*(1093748*mu-4430461)+6714600)-4497720)+
      1115520)+mu*((1476984-470862*mu)*mu-1542320)+533120)+mu*(186240*mu-
      405120)+220160)-62720*mu+71680)+14336)*t/4194304;
    t *= s;
    h[4] = (f*(f*(f*(mu*((466606-141506*mu)*mu-515448)+190400)+mu*(49749*mu-
      113760)+65520)-14400*mu+17280)+2688)*t/2097152;
    t *= s;
    h[5] = (f*(f*(mu*(158532*mu-375991)+225456)-40194*mu+49896)+6336)*t/
      12582912;
    t *= s;
    h[6] = (f*(4004-3146*mu)+429)*t/2097152;
    t *= s;
    h[7] = 715*t/8388608;
#endif
  }

} // namespace GeographicLib

