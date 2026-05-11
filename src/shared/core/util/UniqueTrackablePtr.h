#pragma once

#include <atomic>
#include <memory>
#include <utility>

namespace engine::core::util
{
	/// Bloc de contrôle partagé entre un UniqueTrackablePtr et ses TrackerRef.
	/// Contient le pointeur courant et un flag d'expiration mis à true quand
	/// l'owner libère l'objet.
	template <class T>
	struct TrackerBlock
	{
		T* ptr = nullptr;
		std::atomic<bool> expired{false};
	};

	/// Référence faible non-propriétaire vers un objet possédé par un
	/// UniqueTrackablePtr. Get() retourne nullptr dès que l'owner est détruit
	/// ou Reset(). Sûr en multi-thread (1 atomic load par Get()).
	///
	/// Comparé à std::weak_ptr<T> : pas de double comptage, pas besoin de
	/// .lock() ; le bloc de contrôle est plus léger (1 ptr + 1 atomic bool).
	/// Un TrackerRef est donc moins cher qu'un weak_ptr quand on n'a pas
	/// besoin de prolonger la vie de l'objet.
	///
	/// Cas d'usage type : un Spell vise un Unit. Si l'Unit meurt avant la
	/// résolution du sort, m_target.Get() retourne nullptr → on annule
	/// proprement au lieu de crasher.
	template <class T>
	class TrackerRef
	{
	public:
		TrackerRef() noexcept = default;

		explicit TrackerRef(std::shared_ptr<TrackerBlock<T>> block) noexcept
			: m_block(std::move(block))
		{
		}

		/// Pointeur valide ou nullptr si l'owner a libéré l'objet.
		T* Get() const noexcept
		{
			if (!m_block)
			{
				return nullptr;
			}
			if (m_block->expired.load(std::memory_order_acquire))
			{
				return nullptr;
			}
			return m_block->ptr;
		}

		T* operator->() const noexcept { return Get(); }
		T& operator*() const noexcept { return *Get(); }
		explicit operator bool() const noexcept { return Get() != nullptr; }

		/// Désassocie ce ref du tracker. Get() retournera nullptr ensuite.
		void Reset() noexcept { m_block.reset(); }

	private:
		std::shared_ptr<TrackerBlock<T>> m_block;
	};

	/// Smart pointer propriétaire qui expose des TrackerRef faibles. À la
	/// destruction (ou à Reset/Release) tous les TrackerRef partagés voient
	/// Get() == nullptr immédiatement.
	///
	/// Sémantique : ownership unique (move-only, pas copiable). L'objet
	/// pointé doit avoir été alloué avec `new T(...)`. Le destructeur fait
	/// `delete`.
	///
	/// Le bloc de contrôle est partagé via std::shared_ptr — c'est lui qui
	/// permet aux TrackerRef de coexister avec l'owner unique.
	template <class T>
	class UniqueTrackablePtr
	{
	public:
		UniqueTrackablePtr() noexcept = default;

		/// Prend ownership exclusive de \a obj (typiquement un `new T(...)`).
		/// \a obj == nullptr est légal et produit un pointeur vide.
		explicit UniqueTrackablePtr(T* obj)
		{
			if (obj != nullptr)
			{
				m_block = std::make_shared<TrackerBlock<T>>();
				m_block->ptr = obj;
			}
		}

		~UniqueTrackablePtr() { ResetInternal(); }

		UniqueTrackablePtr(const UniqueTrackablePtr&) = delete;
		UniqueTrackablePtr& operator=(const UniqueTrackablePtr&) = delete;

		UniqueTrackablePtr(UniqueTrackablePtr&& other) noexcept
			: m_block(std::move(other.m_block))
		{
		}

		UniqueTrackablePtr& operator=(UniqueTrackablePtr&& other) noexcept
		{
			if (this != &other)
			{
				ResetInternal();
				m_block = std::move(other.m_block);
			}
			return *this;
		}

		T* Get() const noexcept { return m_block ? m_block->ptr : nullptr; }
		T* operator->() const noexcept { return Get(); }
		T& operator*() const noexcept { return *Get(); }
		explicit operator bool() const noexcept { return Get() != nullptr; }

		/// Crée une référence faible vers l'objet possédé. Tant que cet
		/// UniqueTrackablePtr existe, le ref est valide. Après destruction,
		/// le ref devient nullptr.
		TrackerRef<T> Track() const noexcept
		{
			return TrackerRef<T>(m_block);
		}

		/// Détruit l'objet et invalide tous les TrackerRef.
		void Reset() noexcept { ResetInternal(); }

		/// Cède l'ownership : l'objet n'est PAS détruit, mais les TrackerRef
		/// sont invalidés (puisqu'ils ne peuvent plus garantir la durée de
		/// vie). Le caller devient responsable du delete.
		T* Release() noexcept
		{
			if (!m_block)
			{
				return nullptr;
			}
			T* p = m_block->ptr;
			m_block->ptr = nullptr;
			m_block->expired.store(true, std::memory_order_release);
			m_block.reset();
			return p;
		}

	private:
		void ResetInternal() noexcept
		{
			if (m_block)
			{
				T* p = m_block->ptr;
				m_block->ptr = nullptr;
				// Release barrier pour que les Get() concurrents voient
				// expired=true AVANT que delete ne s'exécute.
				m_block->expired.store(true, std::memory_order_release);
				delete p;
				m_block.reset();
			}
		}

		std::shared_ptr<TrackerBlock<T>> m_block;
	};
}
