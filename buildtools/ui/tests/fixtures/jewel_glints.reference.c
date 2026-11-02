/* Canonical CPU glints from beta 46a40d3. */
static void findGlints(const rodmesh_t *m, Mtx mv, float scale, float fade, star_t *stars, int *n)
{
	Mtx44 proj;
	guPerspective(proj, FOV_Y, 640.0f / 480.0f, 5.0f, 4000.0f);
	const float lift = VIEW_LIFT / 240.0f;
	/* The case is small in the picture: one direction to the camera serves
	 * all of it. */
	v3 V = vnorm(V3(-mv[0][3], -mv[1][3], -mv[2][3]));
	for(int i = 0; i < m->glintEnd; i++) {
		const float *nn = m->nrm + i * 3;
		v3 N;
		xformDir(mv, V3(nn[0], nn[1], nn[2]), &N);
		N = vmul(N, 1.0f / scale);
		float cosv = vdot(N, V);
		if(cosv <= 0.05f) continue;
		v3 R = vreflect(vmul(V, -1), N);
		float best = 0;
		for(int l = 0; l < 4; l++) {
			float d = vdot(R, glintLamps[l]);
			if(d < glintCosTol) continue;
			float ang = acosf(fminf(d, 1.0f));
			/* The profile is sampled every 20-odd degrees: allow for that,
			 * but fall off fast. */
			float k = 1 - ang / GLINT_TOL;
			best = fmaxf(best, k * k * k * k * fresnel(cosv, IOR) * 16.0f);
		}
		if(best < GLINT_MIN) continue;
		best *= fade;
		const float *pp = m->pos + i * 3;
		v3 P;
		xform(mv, V3(pp[0], pp[1], pp[2]), &P);
		/* To the stage, as the 2D pass draws it. */
		float cx = proj[0][0] * P.x, cw = -P.z;
		float cy = proj[1][1] * P.y + lift * cw;
		float sx = (cx / cw * 0.5f + 0.5f) * 640, sy = (0.5f - cy / cw * 0.5f) * 480;
		/* Keep the strongest few, apart from each other. */
		int j;
		for(j = 0; j < *n; j++) {
			float dx = stars[j].x - sx, dy = stars[j].y - sy;
			if(dx * dx + dy * dy < 30 * 30) break;
		}
		if(j < *n) {
			if(best > stars[j].k) stars[j] = (star_t) {sx, sy, best};
			continue;
		}
		if(*n < MAX_STARS) stars[(*n)++] = (star_t) {sx, sy, best};
		else {
			int w = 0;
			for(int q = 1; q < *n; q++) if(stars[q].k < stars[w].k) w = q;
			if(best > stars[w].k) stars[w] = (star_t) {sx, sy, best};
		}
	}
}
