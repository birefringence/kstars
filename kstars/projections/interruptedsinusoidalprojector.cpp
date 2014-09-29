/*
    <one line to give the program's name and a brief idea of what it does.>
    Copyright (C) <year>  <name of author>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*/

#include "interruptedsinusoidalprojector.h"
#include "ksutils.h"

InterruptedSinusoidalProjector::InterruptedSinusoidalProjector(const ViewParams& p) : Projector(p)
{
    lobes = 6;
}

SkyMap::Projection InterruptedSinusoidalProjector::type() const
{
    return SkyMap::InterruptedSinusoidal;
}

double InterruptedSinusoidalProjector::radius() const
{
    return 1.0;
}

bool InterruptedSinusoidalProjector::unusablePoint(const QPointF& p) const
{
    return false;
}

bool InterruptedSinusoidalProjector::checkVisibility( SkyPoint *p ) const
{
    return true;
}

QVector< Vector2f > InterruptedSinusoidalProjector::groundPoly(SkyPoint* labelpoint, bool* drawLabel) const
{
    return QVector<Vector2f>();
}

SkyPoint InterruptedSinusoidalProjector::fromScreen(const QPointF& p, dms* LST, const dms* lat) const
{
    double A = (0.5*m_vp.width  - p.x())/m_vp.zoomFactor;
    double Y = (0.5*m_vp.height - p.y())/m_vp.zoomFactor;

    double lobeno = floor(A/(2*dms::PI/lobes));
    double lobecenter = (lobeno + 0.5)*2*dms::PI/lobes; 
    double relA = A - lobecenter;

    relA = relA / cos(Y);
    A = relA + lobecenter;

    if( m_vp.useAltAz )
        A = -1.0*A; //Azimuth goes in opposite direction compared to RA

    SkyPoint result;
    if ( m_vp.useAltAz ) {
        dms alt, az;
        alt.setRadians( Y );
        az.setRadians( A + m_vp.focus->az().radians() );
        result.setAlt( alt );
        result.setAz( az );
        result.HorizontalToEquatorial( LST, lat );
    } else {
        dms ra, dec;
        dec.setRadians( Y );
        ra.setRadians( A + m_vp.focus->ra().radians() );
        result.set( ra.reduce(), dec );
        result.EquatorialToHorizontal( LST, lat );
    }
    return result;
}

Vector2f InterruptedSinusoidalProjector::toScreenVec(const SkyPoint* o, bool oRefract, bool* onVisibleHemisphere) const
{
    double Y, dX;
    if ( m_vp.useAltAz ) {
        Y = o->alt().radians();
        dX = m_vp.focus->az().reduce().radians() - o->az().reduce().radians();
    } else {
        Y = o->dec().radians();
        dX = o->ra().reduce().radians() - m_vp.focus->ra().reduce().radians();
    }
    Q_ASSERT( std::isfinite( Y ) && std::isfinite( dX ) );
    dX = KSUtils::reduceAngle(dX, -dms::PI, dms::PI);
    double lobeno = floor(dX/(2*dms::PI/lobes));
    double lobecenter = (lobeno + 0.5)*2*dms::PI/lobes;
    double relX = dX - lobecenter;
    if( onVisibleHemisphere )
        *onVisibleHemisphere = true;
    return Vector2f(0.5*m_vp.width  - m_vp.zoomFactor*(relX*cos(Y) + lobecenter),
                    0.5*m_vp.height - m_vp.zoomFactor*Y);
}

