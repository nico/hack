#!/usr/bin/env python3

import math

# all data from
# https://web.archive.org/web/20170131100357/http://files.cie.co.at/204.xls

# [(λ in nm, X(λ), Y(λ), Z(λ))]
cie_1931 = [
(380,0.001368,0.000039,0.006450),
(385,0.002236,0.000064,0.010550),
(390,0.004243,0.000120,0.020050),
(395,0.007650,0.000217,0.036210),
(400,0.014310,0.000396,0.067850),
(405,0.023190,0.000640,0.110200),
(410,0.043510,0.001210,0.207400),
(415,0.077630,0.002180,0.371300),
(420,0.134380,0.004000,0.645600),
(425,0.214770,0.007300,1.039050),
(430,0.283900,0.011600,1.385600),
(435,0.328500,0.016840,1.622960),
(440,0.348280,0.023000,1.747060),
(445,0.348060,0.029800,1.782600),
(450,0.336200,0.038000,1.772110),
(455,0.318700,0.048000,1.744100),
(460,0.290800,0.060000,1.669200),
(465,0.251100,0.073900,1.528100),
(470,0.195360,0.090980,1.287640),
(475,0.142100,0.112600,1.041900),
(480,0.095640,0.139020,0.812950),
(485,0.057950,0.169300,0.616200),
(490,0.032010,0.208020,0.465180),
(495,0.014700,0.258600,0.353300),
(500,0.004900,0.323000,0.272000),
(505,0.002400,0.407300,0.212300),
(510,0.009300,0.503000,0.158200),
(515,0.029100,0.608200,0.111700),
(520,0.063270,0.710000,0.078250),
(525,0.109600,0.793200,0.057250),
(530,0.165500,0.862000,0.042160),
(535,0.225750,0.914850,0.029840),
(540,0.290400,0.954000,0.020300),
(545,0.359700,0.980300,0.013400),
(550,0.433450,0.994950,0.008750),
(555,0.512050,1.000000,0.005750),
(560,0.594500,0.995000,0.003900),
(565,0.678400,0.978600,0.002750),
(570,0.762100,0.952000,0.002100),
(575,0.842500,0.915400,0.001800),
(580,0.916300,0.870000,0.001650),
(585,0.978600,0.816300,0.001400),
(590,1.026300,0.757000,0.001100),
(595,1.056700,0.694900,0.001000),
(600,1.062200,0.631000,0.000800),
(605,1.045600,0.566800,0.000600),
(610,1.002600,0.503000,0.000340),
(615,0.938400,0.441200,0.000240),
(620,0.854450,0.381000,0.000190),
(625,0.751400,0.321000,0.000100),
(630,0.642400,0.265000,0.000050),
(635,0.541900,0.217000,0.000030),
(640,0.447900,0.175000,0.000020),
(645,0.360800,0.138200,0.000010),
(650,0.283500,0.107000,0.000000),
(655,0.218700,0.081600,0.000000),
(660,0.164900,0.061000,0.000000),
(665,0.121200,0.044580,0.000000),
(670,0.087400,0.032000,0.000000),
(675,0.063600,0.023200,0.000000),
(680,0.046770,0.017000,0.000000),
(685,0.032900,0.011920,0.000000),
(690,0.022700,0.008210,0.000000),
(695,0.015840,0.005723,0.000000),
(700,0.011359,0.004102,0.000000),
(705,0.008111,0.002929,0.000000),
(710,0.005790,0.002091,0.000000),
(715,0.004109,0.001484,0.000000),
(720,0.002899,0.001047,0.000000),
(725,0.002049,0.000740,0.000000),
(730,0.001440,0.000520,0.000000),
(735,0.001000,0.000361,0.000000),
(740,0.000690,0.000249,0.000000),
(745,0.000476,0.000172,0.000000),
(750,0.000332,0.000120,0.000000),
(755,0.000235,0.000085,0.000000),
(760,0.000166,0.000060,0.000000),
(765,0.000117,0.000042,0.000000),
(770,0.000083,0.000030,0.000000),
(775,0.000059,0.000021,0.000000),
(780,0.000042,0.000015,0.000000),
]

# [(λ in nm, S0(λ), S1(λ), S2(λ))]
daylight_comp = [
(300,0.04,0.02,0.00),
(305,3.02,2.26,1.00),
(310,6.00,4.50,2.00),
(315,17.80,13.45,3.00),
(320,29.60,22.40,4.00),
(325,42.45,32.20,6.25),
(330,55.30,42.00,8.50),
(335,56.30,41.30,8.15),
(340,57.30,40.60,7.80),
(345,59.55,41.10,7.25),
(350,61.80,41.60,6.70),
(355,61.65,39.80,6.00),
(360,61.50,38.00,5.30),
(365,65.15,40.20,5.70),
(370,68.80,42.40,6.10),
(375,66.10,40.45,4.55),
(380,63.40,38.50,3.00),
(385,64.60,36.75,2.10),
(390,65.80,35.00,1.20),
(395,80.30,39.20,0.05),
(400,94.80,43.40,-1.10),
(405,99.80,44.85,-0.80),
(410,104.80,46.30,-0.50),
(415,105.35,45.10,-0.60),
(420,105.90,43.90,-0.70),
(425,101.35,40.50,-0.95),
(430,96.80,37.10,-1.20),
(435,105.35,36.90,-1.90),
(440,113.90,36.70,-2.60),
(445,119.75,36.30,-2.75),
(450,125.60,35.90,-2.90),
(455,125.55,34.25,-2.85),
(460,125.50,32.60,-2.80),
(465,123.40,30.25,-2.70),
(470,121.30,27.90,-2.60),
(475,121.30,26.10,-2.60),
(480,121.30,24.30,-2.60),
(485,117.40,22.20,-2.20),
(490,113.50,20.10,-1.80),
(495,113.30,18.15,-1.65),
(500,113.10,16.20,-1.50),
(505,111.95,14.70,-1.40),
(510,110.80,13.20,-1.30),
(515,108.65,10.90,-1.25),
(520,106.50,8.60,-1.20),
(525,107.65,7.35,-1.10),
(530,108.80,6.10,-1.00),
(535,107.05,5.15,-0.75),
(540,105.30,4.20,-0.50),
(545,104.85,3.05,-0.40),
(550,104.40,1.90,-0.30),
(555,102.20,0.95,-0.15),
(560,100.00,0.00,0.00),
(565,98.00,-0.80,0.10),
(570,96.00,-1.60,0.20),
(575,95.55,-2.55,0.35),
(580,95.10,-3.50,0.50),
(585,92.10,-3.50,1.30),
(590,89.10,-3.50,2.10),
(595,89.80,-4.65,2.65),
(600,90.50,-5.80,3.20),
(605,90.40,-6.50,3.65),
(610,90.30,-7.20,4.10),
(615,89.35,-7.90,4.40),
(620,88.40,-8.60,4.70),
(625,86.20,-9.05,4.90),
(630,84.00,-9.50,5.10),
(635,84.55,-10.20,5.90),
(640,85.10,-10.90,6.70),
(645,83.50,-10.80,7.00),
(650,81.90,-10.70,7.30),
(655,82.25,-11.35,7.95),
(660,82.60,-12.00,8.60),
(665,83.75,-13.00,9.20),
(670,84.90,-14.00,9.80),
(675,83.10,-13.80,10.00),
(680,81.30,-13.60,10.20),
(685,76.60,-12.80,9.25),
(690,71.90,-12.00,8.30),
(695,73.10,-12.65,8.95),
(700,74.30,-13.30,9.60),
(705,75.35,-13.10,9.05),
(710,76.40,-12.90,8.50),
(715,69.85,-11.75,7.75),
(720,63.30,-10.60,7.00),
(725,67.50,-11.10,7.30),
(730,71.70,-11.60,7.60),
(735,74.35,-11.90,7.80),
(740,77.00,-12.20,8.00),
(745,71.10,-11.20,7.35),
(750,65.20,-10.20,6.70),
(755,56.45,-9.00,5.95),
(760,47.70,-7.80,5.20),
(765,58.15,-9.50,6.30),
(770,68.60,-11.20,7.40),
(775,66.80,-10.80,7.10),
(780,65.00,-10.40,6.80),
(785,65.50,-10.50,6.90),
(790,66.00,-10.60,7.00),
(795,63.50,-10.15,6.70),
(800,61.00,-9.70,6.40),
(805,57.15,-9.00,5.95),
(810,53.30,-8.30,5.50),
(815,56.10,-8.80,5.80),
(820,58.90,-9.30,6.10),
(825,60.40,-9.55,6.30),
(830,61.90,-9.80,6.50),
]

# λ,Relative spectral power distribution of CIE Standard Illuminant D65
d65_table = [
(300,0.034100),
(305,1.664300),
(310,3.294500),
(315,11.765200),
(320,20.236000),
(325,28.644700),
(330,37.053500),
(335,38.501100),
(340,39.948800),
(345,42.430200),
(350,44.911700),
(355,45.775000),
(360,46.638300),
(365,49.363700),
(370,52.089100),
(375,51.032300),
(380,49.975500),
(385,52.311800),
(390,54.648200),
(395,68.701500),
(400,82.754900),
(405,87.120400),
(410,91.486000),
(415,92.458900),
(420,93.431800),
(425,90.057000),
(430,86.682300),
(435,95.773600),
(440,104.865000),
(445,110.936000),
(450,117.008000),
(455,117.410000),
(460,117.812000),
(465,116.336000),
(470,114.861000),
(475,115.392000),
(480,115.923000),
(485,112.367000),
(490,108.811000),
(495,109.082000),
(500,109.354000),
(505,108.578000),
(510,107.802000),
(515,106.296000),
(520,104.790000),
(525,106.239000),
(530,107.689000),
(535,106.047000),
(540,104.405000),
(545,104.225000),
(550,104.046000),
(555,102.023000),
(560,100.000000),
(565,98.167100),
(570,96.334200),
(575,96.061100),
(580,95.788000),
(585,92.236800),
(590,88.685600),
(595,89.345900),
(600,90.006200),
(605,89.802600),
(610,89.599100),
(615,88.648900),
(620,87.698700),
(625,85.493600),
(630,83.288600),
(635,83.493900),
(640,83.699200),
(645,81.863000),
(650,80.026800),
(655,80.120700),
(660,80.214600),
(665,81.246200),
(670,82.277800),
(675,80.281000),
(680,78.284200),
(685,74.002700),
(690,69.721300),
(695,70.665200),
(700,71.609100),
(705,72.979000),
(710,74.349000),
(715,67.976500),
(720,61.604000),
(725,65.744800),
(730,69.885600),
(735,72.486300),
(740,75.087000),
(745,69.339800),
(750,63.592700),
(755,55.005400),
(760,46.418200),
(765,56.611800),
(770,66.805400),
(775,65.094100),
(780,63.382800),
(785,63.843400),
(790,64.304000),
(795,61.877900),
(800,59.451900),
(805,55.705400),
(810,51.959000),
(815,54.699800),
(820,57.440600),
(825,58.876500),
(830,60.312500),
]

# "Computation" section on https://en.wikipedia.org/wiki/Standard_illuminant#Illuminant_series_D
def x_d(T):
    if 4000 <= T < 7000:
        return 0.244063 + 0.09911 * (10**3 / T) + 2.9678 * (10**6 / T**2) - 4.6070 * (10**9 / T**3)
    if 7000 <= T < 25000:
        return 0.237040 + 0.24748 * (10**3 / T) + 1.9018 * (10**6 / T**2) - 2.0064 * (10**9 / T**3)
    raise ValueError("invalid temp", T)


def d_x_y(T):
    x = x_d(T)
    y = -3.0000 * x**2 + 2.870 * x - 0.275
    return x, y


def m1_m2(T):
    x, y = d_x_y(T)
    M = 0.0241 + 0.2562 * x - 0.7341 * y
    M1 = (-1.3515 -  1.7703 * x +  5.9114 * y) / M
    M2 = ( 0.0300 - 31.4424 * x + 30.0717 * y) / M

    # "In order to match all significant digits of the published data of the
    #  canonical illuminants the values of M1 and M2 have to be rounded to three
    #  decimal places before calculation of S_D."
    M1, M2 = round(M1, 3), round(M2, 3)

    return M1, M2


# spd: "spectral power distribution"
def spd(T):
    M1, M2 = m1_m2(T)
    return [(λ, S0 + M1*S1 + M2*S2) for λ, S0, S1, S2 in daylight_comp]
        

# cct: https://en.wikipedia.org/wiki/Color_temperature#Correlated_color_temperature
# Numbers from end of "Computation" section on https://en.wikipedia.org/wiki/Standard_illuminant#Illuminant_series_D
d50_cct = 5003 # horizon light
d65_cct = 6504 # noon light

d50_spd = spd(d50_cct)
d65_spd = spd(d65_cct)


def XYZ_from_spectrum(spectrum):
    cie_1931_dict = dict((λ, (X, Y, Z)) for λ, X, Y, Z in cie_1931)
    X, Y, Z = 0, 0, 0
    for λ, power in spectrum:
        Xn, Yn, Zn = cie_1931_dict.get(λ, (0, 0, 0))
        X += Xn * power
        Y += Yn * power
        Z += Zn * power
    return X, Y, Z


def xy_from_XYZ(X, Y, Z):
    return X / (X + Y + Z), Y / (X + Y + Z)


print('D50 xy from spectrum:   ', xy_from_XYZ(*XYZ_from_spectrum(d50_spd)))
print('D50 xy from temperature:', d_x_y(d50_cct))

print('D65 xy from computed spectrum: ', xy_from_XYZ(*XYZ_from_spectrum(d65_spd)))
print('D65 xy from tabulated spectrum:', xy_from_XYZ(*XYZ_from_spectrum(d65_table)))
print('D65 xy from temperature:       ', d_x_y(d65_cct))


# spectral locus
horseshoe = [xy_from_XYZ(X, Y, Z) for λ, X, Y, Z in cie_1931]
print(f'M {horseshoe[0][0]},{horseshoe[0][1]}', end='')
for c in horseshoe[1:]:
    print(f'L {c[0]},{c[1]}', end='')
print()


# planckian locus
# https://www.fourmilab.ch/documents/specrend/, "Example and Application: Black
# Body Radiation" (the formula there has an incorrect `pi` for `c1` though --
# it cancels out for xyz, but c1 is wrong I think. Looks like it's using
# https://en.wikipedia.org/wiki/Planck%27s_law#First_and_second_radiation_constants
# but https://en.wikipedia.org/wiki/CIE_1931_color_space#Emissive_case wants
# the original `c_1` as far as I understand).
# https://en.wikipedia.org/wiki/Planckian_locus#International_Temperature_Scale
# is relevant too, though.
def black_body_spectral_radiance(wavelength_in_nm, temperature_in_K):
    # https://en.wikipedia.org/wiki/Planck%27s_law#Different_forms, "Wavelength",
    c = 299_792_458     # speed of light, in m/s
    h = 6.62607015e-34  # planck constant, in m**2 kg / s
    kB = 1.380649e-23   # boltzmann constant, in m**2 kg / (s**2 K)

    wavelength_in_m = wavelength_in_nm * 1e-9
    c1 = 2 * h * c**2
    c2 = h * c / kB
    factor1 = c1 / wavelength_in_m**5
    factor2 = math.exp(c2 / (wavelength_in_m * temperature_in_K)) - 1
    return factor1 / factor2


def black_body_XYZ(temperature_in_K):
    # https://en.wikipedia.org/wiki/CIE_1931_color_space#Emissive_case
    # Omits the multiplication by dλ (== 5nm) since it cancels out when passing
    # the result to xy_from_XYZ().
    X, Y, Z = 0, 0, 0
    for λ, Xλ, Yλ, Zλ in cie_1931:
        spectral_radiance_at_wavelength = black_body_spectral_radiance(λ, temperature_in_K)
        X += spectral_radiance_at_wavelength * Xλ
        Y += spectral_radiance_at_wavelength * Yλ
        Z += spectral_radiance_at_wavelength * Zλ
    return X, Y, Z


def black_body_xy(temperature_in_K):
    return xy_from_XYZ(*black_body_XYZ(temperature_in_K))


for T in range(1_000, 10_000 + 1, 500):
    print(f'{T:5} K, {black_body_xy(T)}')


planckian_locus = [black_body_xy(T) for T in range(1_000, 10_000 + 1, 500)]
print(f'M {planckian_locus[0][0]},{planckian_locus[0][1]}', end='')
for c in planckian_locus[1:]:
    print(f'L {c[0]},{c[1]}', end='')
print()

# XXX chromatic adaptation

def XYZ_from_xy(x, y):
    return x / y, 1.0, (1 - x - y) / y

def matrix_3x3_scale(matrix, s):
    return [[x * s for x in r] for r in matrix]

def matrix_3x3_invert(matrix):
    # https://en.wikipedia.org/wiki/Minor_(linear_algebra)
    # "If A is a square matrix, then the minor of the entry in the i-th row and
    #  j-th column [...] is the determinant of the submatrix formed by deleting
    #  the i-th row and j-th column. [...] The (i, j) cofactor is obtained by
    #  multiplying the minor by (-1)^(i+j). [...]"
    # "The inverse of A is the transpose of the cofactor matrix times the reciprocal of the determinant of A"
    a, b, c = matrix[0]
    d, e, f = matrix[1]
    g, h, i = matrix[2]

    determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)
    assert determinant != 0

    m = [[e * i - f * h, c * h - b * i, b * f - c * e],
         [f * g - d * i, a * i - c * g, c * d - a * f],
         [d * h - e * g, b * g - a * h, a * e - b * d]]
    return matrix_3x3_scale(m, 1.0 / determinant)


def matrix_mult(m1, m2):
    R = len(m1)
    C = len(m2[0])
    assert len(m1[0]) == len(m2)
    K = len(m2)
    return [
        [sum(m1[r][k] * m2[k][c] for k in range(K)) for c in range(C)]
        for r in range(R)
    ]


def matrix_transpose(m):
    return [[m[j][i] for j in range(len(m))] for i in range(len(m[0]))]


def matrix_vec_mult(m, v):
    assert len(m[0]) == len(v)
    return [
        sum(m[i][j] * v[j] for j in range(len(v)))
        for i in range(len(m))
    ]


def matrix_3x3_diag(a, b, c):
    return [[a, 0, 0], [0, b, 0], [0, 0, c]]


def matrix_print(m):
    for row in m:
        print('    ' + ' '.join(f'{f:.7f}' for f in row))


# http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
def XYZ_from_rgb_matrix(r, g, b, W):
    XYZ_matrix_T = [XYZ_from_xy(*r),
                    XYZ_from_xy(*g),
                    XYZ_from_xy(*b)]
    XYZ_matrix = matrix_transpose(XYZ_matrix_T)
    XYZ_matrix_I = matrix_3x3_invert(XYZ_matrix)

    S = matrix_vec_mult(XYZ_matrix_I, W)

    return matrix_mult(XYZ_matrix, matrix_3x3_diag(*S))

# https://en.wikipedia.org/wiki/Illuminant_D65
D65_white_XYZ = [95.047 / 100.0, 100 / 100.0, 108.883 / 100.0]

# https://en.wikipedia.org/wiki/SRGB, "Chromaticity" table
# https://www.w3.org/TR/css-color-4/#predefined-sRGB too.
sRGB_r_xy = [0.6400, 0.3300]
sRGB_g_xy = [0.3000, 0.6000]
sRGB_b_xy = [0.1500, 0.0600]

print("sRGB XYZ_D65 from RGB:")
XYZ_D65_from_sRGB = \
    XYZ_from_rgb_matrix(sRGB_r_xy, sRGB_g_xy, sRGB_b_xy, D65_white_XYZ)
matrix_print(XYZ_D65_from_sRGB)


def adaptation_matrix(W_from, W_to):
    # From ICC v4, E.3 Linearized Bradford transformation
    # (Also Bradford M_A from
    # http://www.brucelindbloom.com/index.html?Eqn_ChromAdapt.html
    # )
    cone_from_XYZ = [[ 0.8951,  0.2664, -0.1614],
                     [-0.7502,  1.7135,  0.0367],
                     [ 0.0389, -0.0685,  1.0296]]

    cone_from = matrix_vec_mult(cone_from_XYZ, W_from)
    cone_to = matrix_vec_mult(cone_from_XYZ, W_to)

    adapt = matrix_3x3_diag(*list(cone_to[i] / cone_from[i] for i in range(3)))

    return matrix_mult(matrix_3x3_invert(cone_from_XYZ),
                       matrix_mult(adapt, cone_from_XYZ))


# D50 -- from the ICC spec, required pcs illuminant
D50_white_XYZ = [0.9642029, 1.0, 0.8249054]

print("D50 from D65 Bradford adaptation matrix:")
adapt_D50_from_D65 = adaptation_matrix(D65_white_XYZ, D50_white_XYZ)
matrix_print(adapt_D50_from_D65)

print("sRGB XYZ_D65 adapted to D50:")
XYZ_D50_from_sRGB = matrix_mult(adapt_D50_from_D65, XYZ_D65_from_sRGB)
matrix_print(XYZ_D50_from_sRGB)

print("sRGB XYZ_D50 computed directly:")
matrix_print(
    XYZ_from_rgb_matrix(sRGB_r_xy, sRGB_g_xy, sRGB_b_xy, D50_white_XYZ))

# https://en.wikipedia.org/wiki/DCI-P3#P3_colorimetry
# https://www.w3.org/TR/css-color-4/#predefined-display-p3 too.
P3_r_xy = [0.680, 0.320]
P3_g_xy = [0.265, 0.690]
P3_b_xy = [0.150, 0.060]

print("Display-P3 XYZ_D65 from RGB:")
XYZ_D65_from_P3 = \
    XYZ_from_rgb_matrix(P3_r_xy, P3_g_xy, P3_b_xy, D65_white_XYZ)
matrix_print(XYZ_D65_from_P3)

print("Display-P3 XYZ_D65 adapted to D50:")
XYZ_D50_from_P3 = matrix_mult(adapt_D50_from_D65, XYZ_D65_from_P3)
matrix_print(XYZ_D50_from_P3)

P3_red = [1.0, 0.0, 0.0]
sRGB_from_XYZ_D50 = matrix_3x3_invert(XYZ_D50_from_sRGB)
XYZ_D50_from_P3_red = matrix_vec_mult(XYZ_D50_from_P3, P3_red)
sRGB_from_P3_red = matrix_vec_mult(sRGB_from_XYZ_D50, XYZ_D50_from_P3_red)
print('sRGB from P3(1.0, 0.0, 0.0):', sRGB_from_P3_red)


# https://www.w3.org/TR/css-color-4/#predefined-sRGB
def linear_from_sRGB(c):
    if c < 0.04045:
        return c / 12.92
    return ((c + 0.055) / 1.055) ** 2.4

P3_less_red_linear = [linear_from_sRGB(241.0 / 255.0), 0.0, 0.0]
XYZ_D50_from_P3_less_red = matrix_vec_mult(XYZ_D50_from_P3, P3_less_red_linear)
sRGB_from_P3_less_red_linear = \
    matrix_vec_mult(sRGB_from_XYZ_D50, XYZ_D50_from_P3_less_red)
print('linear-sRGB from P3(241/255, 0.0, 0.0):', sRGB_from_P3_less_red_linear)

P3_green = [0.0, 1.0, 0.0]
XYZ_D50_from_P3_green = matrix_vec_mult(XYZ_D50_from_P3, P3_green)
sRGB_from_P3_green = matrix_vec_mult(sRGB_from_XYZ_D50, XYZ_D50_from_P3_green)
print('sRGB from P3(0.0, 1.0, 0.0):', sRGB_from_P3_green)

P3_rg_linear = [linear_from_sRGB(100.0/255.0), 1.0, 0.0]
XYZ_D50_from_P3_rg = matrix_vec_mult(XYZ_D50_from_P3, P3_rg_linear)
sRGB_from_P3_rg_linear = matrix_vec_mult(sRGB_from_XYZ_D50, XYZ_D50_from_P3_rg)
print('sRGB from P3(100/255, 1.0, 0.0):', sRGB_from_P3_rg_linear)
