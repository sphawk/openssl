/* crypto/ecdsa/ecs_ossl.c */
/* ====================================================================
 * Copyright (c) 1998-2002 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@OpenSSL.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */

#include "ecdsa.h"
#include <openssl/err.h>
#include <openssl/obj_mac.h>

static ECDSA_SIG *ecdsa_do_sign(const unsigned char *dgst, int dlen, 
		EC_KEY *eckey);
static int ecdsa_sign_setup(EC_KEY *eckey, BN_CTX *ctx_in, BIGNUM **kinvp, 
		BIGNUM **rp);
static int ecdsa_do_verify(const unsigned char *dgst, int dgst_len, 
		ECDSA_SIG *sig, EC_KEY *eckey);

static ECDSA_METHOD openssl_ecdsa_meth = {
	"OpenSSL ECDSA method",
	ecdsa_do_sign,
	ecdsa_sign_setup,
	ecdsa_do_verify,
#if 0
	NULL, /* init     */
	NULL, /* finish   */
#endif
	0,    /* flags    */
	NULL  /* app_data */
};

const ECDSA_METHOD *ECDSA_OpenSSL(void)
{
	return &openssl_ecdsa_meth;
}

static int ecdsa_sign_setup(EC_KEY *eckey, BN_CTX *ctx_in, BIGNUM **kinvp,
		BIGNUM **rp)
{
	BN_CTX   *ctx = NULL;
	BIGNUM	 k,*kinv=NULL,*r=NULL,*order=NULL,*X=NULL;
	EC_POINT *tmp_point=NULL;
	int 	 ret = 0;
	if (!eckey  || !eckey->group || !eckey->pub_key || !eckey->priv_key)
	{
		ECDSAerr(ECDSA_F_ECDSA_SIGN_SETUP, ERR_R_PASSED_NULL_PARAMETER);
		return 0;
	}
	if (ctx_in == NULL) 
	{
		if ((ctx=BN_CTX_new()) == NULL)
		{
		ECDSAerr(ECDSA_F_ECDSA_SIGN_SETUP, ERR_R_MALLOC_FAILURE);
		goto err;
		}
	}
	else
		ctx=ctx_in;

	if ((r = BN_new()) == NULL)
	{
		ECDSAerr(ECDSA_F_ECDSA_SIGN_SETUP, ERR_R_BN_LIB);
		goto err;	
	}
	if ((order = BN_new()) == NULL)
	{
		ECDSAerr(ECDSA_F_ECDSA_SIGN_SETUP, ERR_R_BN_LIB);
		goto err;	
	}
	if ((X = BN_new()) == NULL)
	{
		ECDSAerr(ECDSA_F_ECDSA_SIGN_SETUP, ERR_R_BN_LIB);
		goto err;	
	}
	if ((tmp_point = EC_POINT_new(eckey->group)) == NULL)
	{
		ECDSAerr(ECDSA_F_ECDSA_SIGN_SETUP, ERR_R_EC_LIB);
		goto err;
	}
	if (!EC_GROUP_get_order(eckey->group,order,ctx))
	{
		ECDSAerr(ECDSA_F_ECDSA_SIGN_SETUP, ERR_R_EC_LIB);
		goto err;
	}
	
	do
	{
		/* get random k */	
		BN_init(&k);
		do
			if (!BN_rand_range(&k,order))
			{
				ECDSAerr(ECDSA_F_ECDSA_SIGN_SETUP,
				 ECDSA_R_RANDOM_NUMBER_GENERATION_FAILED);	
				goto err;
			}
		while (BN_is_zero(&k));

		/* compute r the x-coordinate of generator * k */
		if (!EC_POINT_mul(eckey->group, tmp_point, &k, NULL, NULL, ctx))
		{
			ECDSAerr(ECDSA_F_ECDSA_SIGN_SETUP, ERR_R_EC_LIB);
			goto err;
		}
		if (EC_METHOD_get_field_type(EC_GROUP_method_of(eckey->group))
			== NID_X9_62_prime_field)
		{
			if (!EC_POINT_get_affine_coordinates_GFp(eckey->group,
				tmp_point, X, NULL, ctx))
			{
				ECDSAerr(ECDSA_F_ECDSA_SIGN_SETUP,
					ERR_R_EC_LIB);
				goto err;
			}
		}
		else /* NID_X9_62_characteristic_two_field */
		{
			if (!EC_POINT_get_affine_coordinates_GF2m(eckey->group,
				tmp_point, X, NULL, ctx))
			{
				ECDSAerr(ECDSA_F_ECDSA_SIGN_SETUP,
					ERR_R_EC_LIB);
				goto err;
			}
		}
		if (!BN_nnmod(r,X,order,ctx))
		{
			ECDSAerr(ECDSA_F_ECDSA_SIGN_SETUP, ERR_R_BN_LIB);
			goto err;
		}
	}
	while (BN_is_zero(r));

	/* compute the inverse of k */
	if ((kinv = BN_mod_inverse(NULL,&k,order,ctx)) == NULL)
	{
		ECDSAerr(ECDSA_F_ECDSA_SIGN_SETUP, ERR_R_BN_LIB);
		goto err;	
	}

	if (*rp == NULL)
		BN_clear_free(*rp);
	*rp = r;
	if (*kinvp == NULL) 
		BN_clear_free(*kinvp);
	*kinvp = kinv;
	kinv = NULL;
	ret = 1;
err:
	if (!ret)
	{
		if (kinv != NULL) BN_clear_free(kinv);
		if (r != NULL) BN_clear_free(r);
	}
	if (ctx_in == NULL) 
		BN_CTX_free(ctx);
	if (kinv != NULL)
		BN_clear_free(kinv);
	if (order != NULL)
		BN_clear_free(order);
	if (tmp_point != NULL) 
		EC_POINT_free(tmp_point);
	if (X)	BN_clear_free(X);
	BN_clear_free(&k);
	return(ret);
}


static ECDSA_SIG *ecdsa_do_sign(const unsigned char *dgst, int dgst_len, 
		EC_KEY *eckey)
{
	BIGNUM *kinv=NULL,*r=NULL,*s=NULL,*m=NULL,*tmp=NULL,*order=NULL;
	BIGNUM xr;
	BN_CTX *ctx=NULL;
	ECDSA_SIG *ret=NULL;
	ECDSA_DATA *ecdsa;

	ecdsa = ecdsa_check(eckey);

	if (!eckey || !eckey->group || !eckey->pub_key || !eckey->priv_key 
		|| !ecdsa)
	{
		ECDSAerr(ECDSA_F_ECDSA_DO_SIGN, ERR_R_PASSED_NULL_PARAMETER);
		goto err;
	}
	BN_init(&xr);

	if ((ctx = BN_CTX_new()) == NULL || (order = BN_new()) == NULL ||
		(tmp = BN_new()) == NULL || (m = BN_new()) == NULL ||
		(s = BN_new()) == NULL )
	{
		ECDSAerr(ECDSA_F_ECDSA_DO_SIGN, ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (!EC_GROUP_get_order(eckey->group,order,ctx))
	{
		ECDSAerr(ECDSA_F_ECDSA_DO_SIGN, ERR_R_EC_LIB);
		goto err;
	}
	if (dgst_len > BN_num_bytes(order))
	{
		ECDSAerr(ECDSA_F_ECDSA_DO_SIGN,
			ECDSA_R_DATA_TOO_LARGE_FOR_KEY_SIZE);
		goto err;
	}

	if (BN_bin2bn(dgst,dgst_len,m) == NULL)
	{
		ECDSAerr(ECDSA_F_ECDSA_DO_SIGN, ERR_R_BN_LIB);
		goto err;
	}
	do
	{
		if (ecdsa->kinv == NULL || ecdsa->r == NULL)
		{
			if (!ECDSA_sign_setup(eckey,ctx,&kinv,&r))
			{
				ECDSAerr(ECDSA_F_ECDSA_DO_SIGN,
					ERR_R_ECDSA_LIB);
				goto err;
			}
		}
		else
		{
			kinv = ecdsa->kinv;
			ecdsa->kinv = NULL;
			r = ecdsa->r;
			ecdsa->r = NULL;
		}

		if (!BN_mod_mul(tmp,eckey->priv_key,r,order,ctx))
		{
			ECDSAerr(ECDSA_F_ECDSA_DO_SIGN, ERR_R_BN_LIB);
			goto err;
		}
		if (!BN_add(s,tmp,m))
		{
			ECDSAerr(ECDSA_F_ECDSA_DO_SIGN, ERR_R_BN_LIB);
			goto err;
		}
		if (BN_cmp(s,order) > 0)
			BN_sub(s,s,order);
		if (!BN_mod_mul(s,s,kinv,order,ctx))
		{
			ECDSAerr(ECDSA_F_ECDSA_DO_SIGN, ERR_R_BN_LIB);
			goto err;
		}
	}
	while (BN_is_zero(s));

	if ((ret = ECDSA_SIG_new()) == NULL)
	{
		ECDSAerr(ECDSA_F_ECDSA_DO_SIGN, ERR_R_MALLOC_FAILURE);
		goto err;
	}
	if (BN_copy(ret->r, r) == NULL || BN_copy(ret->s, s) == NULL)
	{
		ECDSA_SIG_free(ret);
		ret = NULL;
		ECDSAerr(ECDSA_F_ECDSA_DO_SIGN, ERR_R_BN_LIB);
	}
	
err:
	if (r)
		BN_clear_free(r);
	if (s)
		BN_clear_free(s);
	if (ctx)
		BN_CTX_free(ctx);
	if (m)
		BN_clear_free(m);
	if (tmp)
		BN_clear_free(tmp);
	if (order)
		BN_clear_free(order);
	if (kinv)
		BN_clear_free(kinv);
	return(ret);
}

static int ecdsa_do_verify(const unsigned char *dgst, int dgst_len,
		ECDSA_SIG *sig, EC_KEY *eckey)
{
	BN_CTX *ctx;
	BIGNUM *order=NULL,*u1=NULL,*u2=NULL,*m=NULL,*X=NULL;
	EC_POINT *point=NULL;
	int ret = -1;
	if (!eckey || !eckey->group || !eckey->pub_key || !sig)
	{
		ECDSAerr(ECDSA_F_ECDSA_DO_VERIFY, ECDSA_R_MISSING_PARAMETERS);
		return -1;
	}

	if ((ctx = BN_CTX_new()) == NULL || (order = BN_new()) == NULL ||
		(u1 = BN_new()) == NULL || (u2 = BN_new()) == NULL ||
		(m  = BN_new()) == NULL || (X  = BN_new()) == NULL)
	{
		ECDSAerr(ECDSA_F_ECDSA_DO_VERIFY, ERR_R_MALLOC_FAILURE);
		goto err;
	}
	if (!EC_GROUP_get_order(eckey->group, order, ctx))
	{
		ECDSAerr(ECDSA_F_ECDSA_DO_VERIFY, ERR_R_BN_LIB);
		goto err;
	}

	if (BN_is_zero(sig->r) || BN_get_sign(sig->r) ||
	    BN_ucmp(sig->r, order) >= 0)
	{
		ECDSAerr(ECDSA_F_ECDSA_DO_VERIFY, ECDSA_R_BAD_SIGNATURE);
		ret = 0;
		goto err;
	}
	if (BN_is_zero(sig->s) || BN_get_sign(sig->s) ||
	    BN_ucmp(sig->s, order) >= 0)
	{
		ECDSAerr(ECDSA_F_ECDSA_DO_VERIFY, ECDSA_R_BAD_SIGNATURE);
		ret = 0;
		goto err;
	}

	/* calculate tmp1 = inv(S) mod order */
	if ((BN_mod_inverse(u2,sig->s,order,ctx)) == NULL)
	{
		ECDSAerr(ECDSA_F_ECDSA_DO_VERIFY, ERR_R_BN_LIB);
		goto err;
	}
	/* digest -> m */
	if (BN_bin2bn(dgst,dgst_len,m) == NULL)
	{
		ECDSAerr(ECDSA_F_ECDSA_DO_VERIFY, ERR_R_BN_LIB);
		goto err;
	}
	/* u1 = m * tmp mod order */
	if (!BN_mod_mul(u1,m,u2,order,ctx))
	{
		ECDSAerr(ECDSA_F_ECDSA_DO_VERIFY, ERR_R_BN_LIB);
		goto err;
	}
	/* u2 = r * w mod q */
	if (!BN_mod_mul(u2,sig->r,u2,order,ctx))
	{
		ECDSAerr(ECDSA_F_ECDSA_DO_VERIFY, ERR_R_BN_LIB);
		goto err;
	}

	if ((point = EC_POINT_new(eckey->group)) == NULL)
	{
		ECDSAerr(ECDSA_F_ECDSA_DO_VERIFY, ERR_R_MALLOC_FAILURE);
		goto err;
	}
	if (!EC_POINT_mul(eckey->group, point, u1, eckey->pub_key, u2, ctx))
	{
		ECDSAerr(ECDSA_F_ECDSA_DO_VERIFY, ERR_R_EC_LIB);
		goto err;
	}
	if (EC_METHOD_get_field_type(EC_GROUP_method_of(eckey->group))
		== NID_X9_62_prime_field) 
	{
		if (!EC_POINT_get_affine_coordinates_GFp(eckey->group,
			point, X, NULL, ctx))
		{
			ECDSAerr(ECDSA_F_ECDSA_SIGN_SETUP, ERR_R_EC_LIB);
			goto err;
		}
	}
	else /* NID_X9_62_characteristic_two_field */
	{
		if (!EC_POINT_get_affine_coordinates_GF2m(eckey->group,
			point, X, NULL, ctx))
		{
			ECDSAerr(ECDSA_F_ECDSA_SIGN_SETUP, ERR_R_EC_LIB);
			goto err;
		}
	}
	
	if (!BN_nnmod(u1,X,order,ctx))
	{
		ECDSAerr(ECDSA_F_ECDSA_DO_VERIFY, ERR_R_BN_LIB);
		goto err;
	}

	/*  is now in u1.  If the signature is correct, it will be
	 * equal to R. */
	ret = (BN_ucmp(u1,sig->r) == 0);

	err:
	if (ctx)
		BN_CTX_free(ctx);
	if (u1)
		BN_clear_free(u1);
	if (u2)
		BN_clear_free(u2);
	if (m)
		BN_clear_free(m);
	if (X)
		BN_clear_free(X);
	if (order)
		BN_clear_free(order);
	if (point)
		EC_POINT_free(point);
	return(ret);
}
