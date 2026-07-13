#pragma once

// This enum tells the factory which component
// of a vector field you want:
enum Coord {
    SCALAR,     // unified scalar field (pressure, SST, AT, cloud, precip, RH, lightning, sea depth)
    U,          // zonal component
    V,          // meridional component
    MAG,        // magnitude sqrt(U^2 + V^2)
    DIR         // meteorological direction (atan2)
};