// Open New Vegas — road network. Polylines in world coordinates.

const ROADS = [
  {
    name: "Interstate 15 (the Long 15)", kind: "highway",
    points: [
      [18, 2], [21, 7], [25, 13], [26, 19], [27, 26], [28, 31], [29, 35],
      [33, 41], [37, 46], [42, 51], [46, 55], [49, 59], [50, 63], [50, 68],
      [52, 74], [55, 80], [58, 86], [60, 92],
    ],
  },
  {
    name: "Highway 95", kind: "highway",
    points: [
      [59, 45], [58, 40], [57, 34], [56, 27], [58, 23], [62, 21], [65, 18],
      [64, 13], [61, 9], [60, 8], [62, 5],
    ],
  },
  {
    name: "Highway 93 (Boulder Highway)", kind: "highway",
    points: [
      [53, 62], [55, 57], [57, 51], [59, 45], [63, 46], [70, 48], [76, 50],
      [81, 51], [85, 52],
    ],
  },
  {
    name: "Nipton Road (164)", kind: "road",
    points: [[25, 11], [30, 8], [35, 6], [42, 7], [48, 9], [54, 10], [59, 9]],
  },
  {
    name: "Highway 160", kind: "road",
    points: [[44, 53], [37, 52], [29, 52], [22, 52], [16, 53]],
  },
  {
    name: "Red Rock spur", kind: "road",
    points: [[16, 53], [14, 57], [14, 60]],
  },
  {
    name: "Cottonwood road", kind: "road",
    points: [[62, 5], [66, 5], [68, 12], [70, 8], [70, 3]],
  },
  {
    name: "Nellis approach", kind: "road",
    points: [[52, 74], [57, 76], [61, 78], [64, 79]],
  },
  {
    name: "Kyle Canyon road", kind: "road",
    points: [[46, 55], [38, 60], [30, 66], [22, 72], [16, 75], [12, 77]],
  },
];

if (typeof module !== "undefined") module.exports = { ROADS };
