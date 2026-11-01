/* Frozen direct lookup from accepted 46a40d3; compare interpolation bit for bit. */
static v3 referenceLightingLookup(const v3 *table, v3 d)
{
	float ax = fabsf(d.x), ay = fabsf(d.y), az = fabsf(d.z), u, v, m;
	int f;
	if(ax >= ay && ax >= az) { f = d.x > 0 ? 0 : 1; m = ax; u = d.x > 0 ? -d.z : d.z; v = d.y; }
	else if(ay >= az) { f = d.y > 0 ? 2 : 3; m = ay; u = d.x; v = d.y > 0 ? -d.z : d.z; }
	else { f = d.z > 0 ? 4 : 5; m = az; u = d.z > 0 ? d.x : -d.x; v = d.y; }
	if(!(m > 0.0f)) return table[0];
	float fx = clampf((u / m + 1) * 0.5f * ROOM_RES - 0.5f, 0, ROOM_RES - 1.001f);
	float fy = clampf((v / m + 1) * 0.5f * ROOM_RES - 0.5f, 0, ROOM_RES - 1.001f);
	int x0 = (int)fx, y0 = (int)fy;
	float tx = fx - x0, ty = fy - y0;
	const v3 *t = table + (f * ROOM_RES + y0) * ROOM_RES + x0;
	v3 a = vmix(t[0], t[1], tx), b = vmix(t[ROOM_RES], t[ROOM_RES + 1], tx);
	return vmix(a, b, ty);
}
