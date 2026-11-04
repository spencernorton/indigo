import unittest

from client import decode_coordinates


class CoordinateDecodeTests(unittest.TestCase):
    def setUp(self):
        self.width, self.height = 720, 480
        levels = (16, 80, 160, 235)
        self.image = bytearray(self.width * self.height * 2)
        for y in range(self.height):
            for x in range(self.width):
                at = (y * self.width + x) * 2
                self.image[at] = levels[((x >> ((y // 6) % 10)) & 1) +
                                        2 * ((y >> ((x // 6) % 9)) & 1)]
                self.image[at + 1] = 128

    def test_all_columns_and_rows(self):
        result = decode_coordinates(self.image, self.width, self.height, self.width)
        self.assertTrue(result['columns_match'])
        self.assertTrue(result['rows_match'])
        self.assertEqual(result['distinct_columns'], 720)
        self.assertEqual(result['distinct_rows'], 480)

    def test_repeated_additional_columns_do_not_pass(self):
        for y in range(self.height):
            start = y * self.width * 2
            self.image[start + 640 * 2:start + 720 * 2] = self.image[start:start + 80 * 2]
        result = decode_coordinates(self.image, self.width, self.height, self.width)
        self.assertFalse(result['columns_match'])
        self.assertEqual(result['distinct_columns'], 640)

    def test_last_row_is_not_ignored(self):
        row = self.width * 2
        self.image[-row:] = self.image[-2 * row:-row]
        result = decode_coordinates(self.image, self.width, self.height, self.width)
        self.assertFalse(result['rows_match'])
        self.assertEqual(result['distinct_rows'], 479)


if __name__ == '__main__':
    unittest.main()
