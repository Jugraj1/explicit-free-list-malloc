#include "testing.h"

/**
 * This test allocates a large amount of pointers and then frees them in a 'random'
 * order.
 */

#define NALLOCS 1024

int orders[] = {987, 944, 639, 1013, 128, 820, 600, 92, 394, 278, 801, 939, 486, 1008, 384, 328, 839, 653, 571, 20, 533, 331, 288, 56, 320, 824, 746, 332, 302, 173, 338, 983, 585, 69, 656, 995, 159, 217, 592, 684, 980, 895, 715, 982, 841, 349, 154, 188, 289, 190, 250, 6, 261, 738, 761, 100, 857, 248, 750, 295, 693, 652, 716, 890, 621, 130, 846, 315, 532, 705, 358, 877, 812, 523, 58, 852, 287, 912, 682, 512, 876, 795, 919, 517, 430, 692, 667, 922, 86, 826, 956, 861, 544, 17, 534, 469, 209, 631, 757, 503, 777, 837, 203, 968, 696, 461, 171, 915, 932, 362, 26, 562, 355, 233, 123, 2, 90, 91, 619, 329, 726, 135, 235, 367, 473, 634, 563, 147, 407, 31, 903, 67, 701, 723, 719, 259, 818, 23, 474, 712, 904, 242, 649, 745, 71, 946, 232, 204, 992, 245, 399, 747, 608, 539, 457, 501, 576, 815, 132, 587, 51, 308, 821, 875, 348, 427, 754, 771, 581, 629, 772, 117, 591, 68, 865, 740, 993, 174, 950, 908, 823, 201, 80, 133, 854, 785, 129, 784, 537, 588, 535, 163, 1012, 834, 481, 178, 1023, 252, 464, 554, 781, 301, 423, 997, 282, 558, 323, 13, 836, 994, 412, 419, 352, 27, 63, 342, 477, 819, 703, 612, 142, 115, 202, 307, 555, 565, 346, 452, 626, 436, 769, 152, 677, 160, 1003, 483, 425, 713, 32, 333, 14, 239, 453, 316, 251, 666, 893, 1018, 210, 182, 218, 899, 18, 22, 604, 728, 271, 1009, 963, 909, 1014, 969, 597, 955, 44, 577, 551, 770, 21, 970, 902, 140, 344, 137, 205, 580, 476, 38, 268, 426, 74, 267, 943, 102, 674, 647, 33, 730, 776, 998, 484, 816, 910, 850, 405, 868, 417, 936, 793, 353, 230, 538, 552, 858, 337, 599, 896, 773, 611, 81, 706, 445, 736, 658, 28, 42, 420, 249, 475, 12, 482, 262, 493, 880, 574, 1010, 398, 805, 199, 106, 775, 36, 550, 1021, 29, 485, 347, 870, 767, 620, 95, 617, 124, 664, 536, 40, 492, 429, 356, 299, 756, 725, 665, 564, 949, 428, 52, 165, 739, 151, 807, 254, 360, 317, 7, 799, 782, 247, 622, 61, 341, 240, 593, 680, 957, 462, 616, 699, 881, 153, 283, 741, 646, 272, 1019, 179, 614, 361, 8, 765, 84, 103, 258, 313, 197, 257, 996, 804, 508, 624, 567, 789, 961, 828, 351, 985, 872, 694, 934, 883, 640, 759, 755, 291, 582, 686, 354, 198, 737, 510, 131, 211, 788, 37, 378, 224, 940, 496, 395, 87, 691, 810, 917, 931, 735, 85, 679, 768, 5, 296, 1001, 953, 381, 433, 602, 572, 657, 408, 927, 432, 661, 255, 569, 1004, 794, 892, 669, 196, 30, 231, 683, 618, 504, 107, 144, 511, 518, 814, 194, 122, 800, 672, 497, 989, 779, 991, 590, 266, 878, 280, 141, 98, 187, 73, 401, 778, 901, 213, 298, 502, 105, 521, 977, 603, 312, 526, 318, 375, 546, 605, 853, 589, 530, 999, 1022, 973, 925, 942, 447, 451, 543, 873, 440, 488, 918, 566, 568, 78, 984, 838, 379, 906, 366, 50, 632, 145, 82, 659, 400, 720, 322, 809, 409, 930, 265, 93, 879, 763, 586, 463, 913, 547, 495, 119, 155, 269, 643, 662, 243, 945, 206, 796, 489, 1002, 397, 162, 300, 808, 223, 446, 685, 527, 276, 914, 829, 832, 642, 811, 114, 39, 707, 660, 843, 25, 641, 976, 708, 357, 979, 330, 382, 214, 491, 294, 718, 627, 229, 222, 859, 1020, 319, 531, 277, 898, 570, 350, 219, 687, 60, 863, 813, 403, 458, 1017, 885, 96, 238, 377, 459, 742, 396, 673, 974, 710, 111, 889, 734, 704, 848, 866, 638, 727, 894, 959, 764, 55, 195, 648, 390, 905, 583, 514, 924, 479, 449, 596, 416, 402, 842, 324, 907, 234, 613, 540, 263, 363, 887, 921, 651, 177, 541, 671, 414, 519, 972, 281, 336, 774, 66, 981, 207, 549, 520, 751, 227, 964, 19, 891, 744, 369, 798, 786, 601, 758, 241, 109, 578, 180, 766, 937, 321, 46, 4, 138, 365, 515, 689, 668, 364, 960, 340, 722, 220, 595, 465, 270, 192, 450, 387, 625, 274, 678, 181, 161, 57, 856, 559, 15, 654, 958, 978, 711, 221, 373, 560, 579, 43, 415, 10, 454, 372, 688, 471, 112, 424, 916, 97, 256, 882, 780, 303, 752, 89, 404, 371, 717, 88, 167, 845, 926, 928, 120, 988, 986, 844, 164, 633, 134, 1007, 867, 695, 869, 884, 690, 260, 326, 500, 762, 1016, 610, 487, 334, 443, 434, 146, 676, 840, 136, 275, 422, 388, 609, 108, 967, 76, 749, 9, 847, 121, 385, 498, 783, 72, 606, 663, 157, 168, 655, 53, 94, 448, 175, 292, 297, 966, 507, 143, 149, 286, 748, 139, 650, 200, 830, 306, 309, 635, 16, 421, 418, 189, 525, 817, 548, 244, 929, 468, 380, 790, 439, 505, 54, 383, 376, 975, 59, 509, 343, 176, 64, 556, 802, 116, 101, 954, 472, 110, 150, 11, 971, 438, 681, 920, 113, 803, 923, 228, 389, 516, 212, 226, 170, 1011, 860, 855, 264, 1000, 791, 470, 584, 1005, 49, 35, 345, 166, 797, 183, 442, 952, 411, 41, 62, 1, 156, 467, 670, 368, 359, 406, 490, 191, 236, 126, 499, 700, 951, 760, 391, 529, 273, 732, 466, 47, 208, 522, 598, 455, 822, 513, 962, 886, 393, 185, 835, 849, 644, 941, 833, 714, 339, 104, 118, 636, 628, 392, 413, 431, 386, 545, 3, 24, 253, 623, 743, 304, 561, 948, 314, 237, 186, 435, 158, 675, 933, 697, 77, 594, 45, 125, 1006, 792, 460, 851, 184, 888, 65, 733, 478, 246, 637, 645, 99, 947, 216, 293, 70, 709, 528, 864, 494, 990, 575, 753, 48, 900, 311, 831, 729, 335, 1015, 862, 75, 193, 935, 290, 825, 327, 172, 325, 615, 965, 225, 370, 607, 456, 827, 169, 542, 285, 480, 897, 0, 441, 127, 871, 874, 938, 702, 787, 806, 374, 630, 310, 148, 410, 83, 215, 305, 911, 284, 279, 79, 553, 557, 444, 724, 698, 506, 34, 731, 573, 524, 437, 721};

int main(void) {
  void *ptrs[NALLOCS];
  mallocing_loop(ptrs, 8, NALLOCS);
  for (int i = 0; i < NALLOCS; i++) {
    freeing(ptrs[orders[i]]);
  }
  return 0;
}
