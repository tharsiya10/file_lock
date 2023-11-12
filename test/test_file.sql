
--\! echo EXO_0:

DROP TABLE IF EXISTS audit;
DROP TABLE IF EXISTS comptes;
CREATE TABLE comptes (
	numcompte integer PRIMARY KEY,
	nomclient varchar(20) NOT NULL,
	solde numeric(10,2) NOT NULL,
	decouvert_autorise numeric(10,2) NOT NULL DEFAULT 0
	CHECK (decouvert_autorise <=0)
);

INSERT INTO comptes(numcompte, nomclient, solde, decouvert_autorise) VALUES (1, 'II1', 10, -100);
INSERT INTO comptes(numcompte, nomclient, solde, decouvert_autorise) VALUES (2, 'II2', 10, -100);
INSERT INTO comptes(numcompte, nomclient, solde, decouvert_autorise) VALUES (3, 'II3', 0, -10);
INSERT INTO comptes(numcompte, nomclient, solde, decouvert_autorise) VALUES (4, 'II4', -10, -10);
INSERT INTO comptes(numcompte, nomclient, solde, decouvert_autorise) VALUES (5, 'III1', 100, -100);
INSERT INTO comptes(numcompte, nomclient, solde, decouvert_autorise) VALUES (6, 'III2', 2, -100);
INSERT INTO comptes(numcompte, nomclient, solde, decouvert_autorise) VALUES (7, 'IV1', 0, 0);
INSERT INTO comptes(numcompte, nomclient, solde, decouvert_autorise) VALUES (8, 'IV2', 100, 0);
INSERT INTO comptes(numcompte, nomclient, solde, decouvert_autorise) VALUES (9, 'V1', 1000, -100);
INSERT INTO comptes(numcompte, nomclient, solde, decouvert_autorise) VALUES (10, 'V2', 1000, -10);

--\! echo EXO_0: ajout d'une fonction de retrait a partir d'un compte

CREATE OR REPLACE FUNCTION retrait (nom VARCHAR, montant NUMERIC(10,2)) RETURNS VOID AS $$
DECLARE
	nbcomptes INTEGER;
BEGIN
	SELECT count(*) INTO nbcomptes FROM comptes WHERE nomclient = nom;
	
	IF (nbcomptes > 1) THEN RAISE 'Plusiers comptes pour le client %', nom USING ERRCODE ='20002';
	ELSEIF (nbcomptes < 1) THEN RAISE 'Aucun compte pour le client %', nom USING ERRCODE ='20002';
	ELSE UPDATE comptes SET solde = solde - montant WHERE nomclient = nom; 
	END IF;
END;
$$ LANGUAGE plpgsql;



--\! echo Requete 2.1: Le solde d’un compte ne doit jamais être inférieur au découvert autorisé
ALTER TABLE comptes ADD CHECK (solde >= decouvert_autorise);




--\! echo Requete 2.2: Si le solde d’un compte devient négatif, l’utilisateur doit être averti

CREATE OR REPLACE FUNCTION alert_solde_negatif() RETURNS TRIGGER AS $$   
BEGIN    
    RAISE NOTICE 'Solde negatif du compte % : % €', NEW.numcompte, NEW.solde;
    RETURN NULL;    	-- ignoree
END;
$$ LANGUAGE plpgsql;



DROP TRIGGER IF EXISTS notif_solde_negatif ON comptes;
CREATE TRIGGER notif_solde_negatif AFTER UPDATE OF solde OR INSERT on comptes
FOR EACH ROW WHEN (NEW.solde < 0) EXECUTE PROCEDURE alert_solde_negatif();


--SELECT retrait ('IV1', 19);


--\! echo Requete 2.3: Un compte ne peut être fermé (supprimé de la table) que si le solde est 0.

CREATE OR REPLACE FUNCTION abandon_cloture_solde_nonnull() RETURNS TRIGGER AS $$   
BEGIN    
    RAISE NOTICE 'Peut pas cloturer le compte avec le solde != 0';
    RETURN NULL;     --abandon
END;
$$ LANGUAGE plpgsql;


DROP TRIGGER IF EXISTS trig_abandon_cloture_solde_nonnull ON comptes;
CREATE TRIGGER trig_abandon_cloture_solde_nonnull BEFORE DELETE ON comptes FOR EACH ROW WHEN (OLD.solde != 0) EXECUTE PROCEDURE abandon_cloture_solde_nonnull();

--DELETE FROM comptes WHERE numcompte = 1;
--DELETE FROM comptes WHERE numcompte = 3;




--\! echo Requete 3: La banque a décidé d’appliquer des frais de 5% à toutes les transactions de retrait. Implémentez un trigg qui met en place cette nouvelle règle de gestion.

CREATE OR REPLACE FUNCTION frais_retrait() RETURNS TRIGGER AS $$
BEGIN
    NEW.solde = NEW.solde - (OLD.solde - NEW.solde) * 0.05;
    RETURN NEW; 
END;
$$ LANGUAGE plpgsql;



DROP TRIGGER IF EXISTS trig_frais_retrait ON comptes;
CREATE TRIGGER trig_frais_retrait BEFORE UPDATE OF solde ON comptes
FOR EACH ROW WHEN (NEW.solde < OLD.solde) EXECUTE PROCEDURE frais_retrait();


--SELECT retrait ('III1', 100);
--SELECT retrait ('III2', 100);



--\! EXO4: La banque doit maintenir une trace de toutes les opérations portées sur les comptes bancaires. La table ci-dessous maintient l’historique des transactions.

DROP TABLE  IF EXISTS audit;
CREATE TABLE audit (
	numoperation SERIAL PRIMARY KEY,
	numcompte INTEGER NOT NULL REFERENCES comptes,
	date_operation DATE NOT NULL,
	operation VARCHAR(10) NOT NULL CHECK (operation in ('RETRAIT', 'OUVERTURE', 'DEPOT', 'FERMETURE')), 
	montant numeric(10,2)
);

--\! echo Requete 4: Écrivez un trigger qui peuple la table audit à chaque transaction. Prenez soin de faire en sorte que seules les opérations qui ont été complétées apparaissent dans la table d’audit. Par exemple, si un utilisateur tente de retirer un montant et que le solde devient inférieur au découvert autorisé, l’opération est refusée et elle ne devra donc pas apparaître dans la table audit.

/* on delete cascade doesn;t work
	because trigger is AFTER. so need tu insert (2, null, 2023-03-12, FERMETURE, 0.00), but numcompte can't be null
	=> need to make 2 triggers before abandon_cloture_solde_nonnull
*/

-- Operations: ’RETRAIT’, ’OUVERTURE’, ’DEPOT’, ’FERMETURE’

CREATE OR REPLACE FUNCTION my_audit() RETURNS TRIGGER AS $$
DECLARE 
	montant int := OLD.solde - NEW.solde;
BEGIN
	IF TG_OP = 'INSERT' THEN INSERT INTO audit(numcompte, date_operation, operation, montant) VALUES (NEW.numcompte, CURRENT_DATE, 'OUVERTURE', montant); RETURN NEW;
	ELSEIF (TG_OP = 'UPDATE') AND (NEW.solde < OLD.solde) THEN INSERT INTO audit(numcompte, date_operation, operation, montant) VALUES (NEW.numcompte, CURRENT_DATE, 'RETRAIT', montant); RETURN NEW;
	ELSEIF (TG_OP = 'UPDATE') AND (NEW.solde > OLD.solde) THEN INSERT INTO audit(numcompte, date_operation, operation, montant) VALUES (NEW.numcompte, CURRENT_DATE, 'DEPOT', montant); RETURN NEW;
	ELSEIF (TG_OP = 'DELETE') THEN INSERT INTO audit(numcompte, date_operation, operation, montant) VALUES (NEW.numcompte, CURRENT_DATE, 'FERMETURE', montant); RETURN NEW;
	ELSE RAISE 'Insupported operation' USING ERRCODE='20001';
	END IF;
END;
$$ LANGUAGE plpgsql;


DROP TRIGGER IF EXISTS trig_frais_retrait ON comptes;
CREATE TRIGGER trig_audit AFTER UPDATE OR INSERT OR DELETE ON comptes FOR EACH ROW EXECUTE PROCEDURE my_audit();






