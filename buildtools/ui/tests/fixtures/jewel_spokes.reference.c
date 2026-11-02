/* Canonical emitter from beta 46a40d3; retained for exact-stream regression. */
static void drawSpokes(Mtx mv, float zFace, float nzLocal, float fade)
{
	const v3 *L[4] = {&KEY, &STRIP, &TOP, &BOUNCE};
	const float gain[4] = {1.6f, 1.4f, 0.9f, 0.35f}, spread[4] = {0.05f, 0.045f, 0.06f, 0.16f};
	v3 centre, N, ex, ey;
	if(fade <= 0.0f) return;
	xform(mv, V3(0, 0, zFace), &centre);
	xformDir(mv, V3(0, 0, nzLocal), &N);
	xformDir(mv, V3(1, 0, 0), &ex);
	xformDir(mv, V3(0, 1, 0), &ey);
	float sc = vlen(ex);
	N = vnorm(N);
	ex = vnorm(ex);
	ey = vnorm(ey);
	v3 V0 = vnorm(vmul(centre, -1));
	if(vdot(N, V0) <= 0.0f) return;

	GX_SetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_CLEAR);
	for(int l = 0; l < 4; l++) {
		v3 H = vadd(*L[l], V0);
		float hx = vdot(H, ex), hy = vdot(H, ey);
		float hl = caseSqrt(hx * hx + hy * hy);
		float theta = atan2f(hy, hx);
		float dth = fminf(spread[l] * 2.2f / fmaxf(hl, 1e-3f), 0.5f);
		for(int side = 0; side < 2; side++) {
			float th = theta + side * PI_F;
			enum { RS = 12, CS = 4 };
			/* The directions across the band, and the silver under its two
			 * edges on each ring, to blend between (it is narrow, the silver
			 * smooth across it). */
			float ca[CS + 1], sa[CS + 1];
			for(int ci = 0; ci <= CS; ci++) {
				float a = th + dth * ((ci / (float)CS) * 2.0f - 1.0f);
				ca[ci] = cosf(a);
				sa[ci] = sinf(a);
			}
			v3 under[RS + 1][2];
			float data[RS + 1];
			for(int ri = 0; ri <= RS; ri++) {
				float r = 21.5f + (58.8f - 21.5f) * ri / RS;
				for(int e = 0; e < 2; e++) {
					v3 P;
					xform(mv, V3(r * ca[e * CS], r * sa[e * CS], zFace), &P);
					v3 V = vnorm(vmul(P, -1));
					under[ri][e] = silver(vreflect(vmul(V, -1), N), N, r);
				}
				data[ri] = smoothstep(21.5f, 23.0f, r) * (1 - smoothstep(58.2f, 58.8f, r));
			}
			for(int ci = 0; ci < CS; ci++) {
				GX_Begin(GX_TRIANGLESTRIP, VTXFMT_DIR, (RS + 1) * 2);
				for(int ri = 0; ri <= RS; ri++) {
					float r = 21.5f + (58.8f - 21.5f) * ri / RS;
					for(int k = 0; k < 2; k++) {
						float cs = ca[ci + k], sn = sa[ci + k];
						float x = r * cs, y = r * sn;
						v3 P;
						xform(mv, V3(x, y, zFace), &P);
						v3 V = vnorm(vmul(P, -1));
						v3 h = vadd(*L[l], V);
						v3 rad = vadd(vmul(ex, cs), vmul(ey, sn));
						v3 tan = vadd(vmul(ex, -sn), vmul(ey, cs));
						float along = vdot(h, tan), u = fabsf(vdot(h, rad));
						float w = expf(-along * along / (spread[l] * spread[l]));
						v3 c = vadd(vadd(spectral(1600 * u), vmul(spectral(800 * u), 0.75f)), vmul(spectral(533.3f * u), 0.45f));
						c = vmul(c, w * gain[l] * 1.3f * data[ri] * fade);
						/* Added on top of the mirror, in display light. */
						v3 base = vmix(under[ri][0], under[ri][1], (float)(ci + k) / CS);
						u8 cr = toSrgb8(base.x + c.x) - toSrgb8(base.x);
						u8 cg = toSrgb8(base.y + c.y) - toSrgb8(base.y);
						u8 cb = toSrgb8(base.z + c.z) - toSrgb8(base.z);
						emitColor(x, y, zFace + nzLocal * 0.02f / sc, cr, cg, cb, 255);
					}
				}
				GX_End();
			}
		}
	}
}
